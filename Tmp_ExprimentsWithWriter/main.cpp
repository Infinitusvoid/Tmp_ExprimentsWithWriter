// FluxGymScriptGen.cpp
// Uses your Writer_::Writer to generate dataset.toml, sample_prompts.txt, and train.bat,
// then (optionally) runs the .bat through cmd. No edits to Writer required.

#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstdlib>      // std::system
#include <cassert>
#include <cctype>       // std::tolower
#include <system_error> // std::error_code

#include "Writer.h"

namespace fs = std::filesystem;

// ---------- Small utilities (no Writer edits) ----------
static std::string toml_quote_win_path(const std::string& p) {
    // TOML double-quoted strings treat backslash as an escape. Make it literal with \\.
    std::string out;
    out.reserve(p.size() * 2);
    for (char c : p) {
        if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}
static bool ensure_dir(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) return fs::is_directory(p, ec);
    return fs::create_directories(p, ec);
}
static std::string leaf_name(const fs::path& p) {
    auto leaf = p.filename().string();
    if (!leaf.empty() && (leaf.back() == '\\' || leaf.back() == '/'))
        leaf.pop_back();
    return leaf;
}
static bool has_caption_for_every_image(const fs::path& image_dir) {
    // Very lightweight heuristic: if there are any images without .txt twin, return false.
    // If no images found, default to false.
    static const char* exts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".webp" };
    bool anyImage = false;
    std::error_code ec;
    if (!fs::exists(image_dir, ec) || !fs::is_directory(image_dir, ec)) return false;
    for (auto& e : fs::directory_iterator(image_dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        bool isImage = false;
        for (auto* x : exts) if (ext == x) { isImage = true; break; }
        if (!isImage) continue;
        anyImage = true;
        auto twin = e.path();
        twin.replace_extension(".txt");
        if (!fs::exists(twin)) return false;
    }
    return anyImage; // true only if there was at least 1 image and all had captions
}

// ---------- Emit TOML ----------
static bool write_dataset_toml(const fs::path& toml_path,
    const std::string& image_dir_win,
    const std::string& trigger_token,
    bool each_image_has_txt,
    int min_reso, int max_reso,
    int reso_x, int reso_y,
    int repeats) {
    Writer_::Writer w;

    // Use ${...} placeholders only where we provide Vars.
    Writer_::Writer::Vars V{
        {"IMG_DIR", toml_quote_win_path(image_dir_win)},
        {"TRIGGER", trigger_token}
    };

    w.line("[general]");
    w.line("shuffle_caption = false");
    w.line("caption_extension = \".txt\"");
    w.line("keep_tokens = 1");
    w.blank();
    w.line("# Bucketing = automatic resize/crop per aspect ratio at load time");
    w.line("enable_bucket = true");
    w.line("bucket_reso_steps = 64");
    w.linef("min_bucket_reso = {}", min_reso);
    w.linef("max_bucket_reso = {}", max_reso);
    w.line("bucket_no_upscale = true  # don't enlarge small images (optional)");
    w.blank();
    w.line("[[datasets]]");
    w.linef("resolution = [{}, {}]", reso_x, reso_y);
    w.line("batch_size = 1");
    w.line("keep_tokens = 1");
    w.blank();
    w.line("[[datasets.subsets]]");
    w.line("image_dir = \"${IMG_DIR}\"", V);

    // IMPORTANT: If you ALREADY have per-image captions, do NOT add class_tokens.
    // If you DO NOT have captions, add class_tokens as a fallback text.
    if (!each_image_has_txt) {
        w.line("class_tokens = \"${TRIGGER}\"", V);
    }
    w.linef("num_repeats = {}", repeats);

    try {
        ensure_dir(toml_path.parent_path());
        w.save(toml_path);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------- Emit sample_prompts.txt ----------
static bool write_sample_prompts(const fs::path& prompts_path,
    const std::string& trigger_token) {
    Writer_::Writer w;
    w.line(trigger_token + ", portrait, high detail");
    w.line(trigger_token + ", in a neon city at night, cinematic lighting");
    w.line(trigger_token + ", full-body, outdoor, volumetric fog");
    try {
        ensure_dir(prompts_path.parent_path());
        w.save(prompts_path);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------- Emit train.bat ----------
struct TrainFlags {
    // Models
    std::string unet_path;  // "F:\\FluxGym\\fluxgym\\models\\unet\\flux1-dev.sft"
    std::string clip_l;     // "F:\\FluxGym\\fluxgym\\models\\clip\\clip_l.safetensors"
    std::string t5xxl;      // "F:\\FluxGym\\fluxgym\\models\\clip\\t5xxl_fp16.safetensors" or fp8
    std::string ae;         // "F:\\FluxGym\\fluxgym\\models\\vae\\ae.sft"

    // Project root (for cd /d)
    std::string root_dir;

    // Script and IO
    std::string sd_scripts_entry; // "sd-scripts/flux_train_network.py" relative to root, or absolute
    std::string dataset_toml;     // outputs\\NAME\\dataset.toml
    std::string sample_prompts;   // outputs\\NAME\\sample_prompts.txt
    std::string out_dir;          // outputs\\NAME
    std::string out_name;         // NAME

    // Env handling
    bool        use_env_activation = true;            // call env\\Scripts\\activate
    std::string env_activate_rel = R"(env\Scripts\activate)";
    bool        auto_install_accel = false;           // pip install accelerate if missing

    // Knobs
    bool        fp8_base = false;        // set to true only if your T5 is FP8
    int         max_epochs = 100;
    int         save_every_n_epochs = 1;
    std::string lr = "8e-4";
    int         seed = 42;
    int         max_workers = 2;
};

static bool write_train_bat(const fs::path& bat_path, const TrainFlags& F) {
    Writer_::Writer::Vars V{
        {"ROOT",               F.root_dir},
        {"SD_ENTRY",           F.sd_scripts_entry},
        {"UNET",               F.unet_path},
        {"CLIP_L",             F.clip_l},
        {"T5XXL",              F.t5xxl},
        {"AE",                 F.ae},
        {"DATASET_TOML",       F.dataset_toml},
        {"SAMPLE_PROMPTS",     F.sample_prompts},
        {"OUT_DIR",            F.out_dir},
        {"OUT_NAME",           F.out_name},
        {"LR",                 F.lr},
        {"SEED",               std::to_string(F.seed)},
        {"MAX_WORKERS",        std::to_string(F.max_workers)},
        {"MAX_EPOCHS",         std::to_string(F.max_epochs)},
        {"SAVE_EVERY_EPOCH",   std::to_string(F.save_every_n_epochs)},
        {"FP8_FLAG",           F.fp8_base ? "--fp8_base" : ""},
        {"USE_ENV_ACT",        F.use_env_activation ? "1" : "0"},
        {"ENV_ACTIVATE_REL",   F.env_activate_rel},
        {"AUTO_INSTALL_ACCEL", F.auto_install_accel ? "1" : "0"}
    };

    const std::string B = R"BATCH(@echo off
setlocal EnableExtensions

REM ====== go to project root ======
set "ROOT=${ROOT}"
cd /d "%ROOT%"

REM ====== (optional) activate your known-good env (like your start script) ======
if "${USE_ENV_ACT}"=="1" (
  if exist "${ENV_ACTIVATE_REL}" (
    echo Activating: ${ENV_ACTIVATE_REL}
    call "${ENV_ACTIVATE_REL}"
  ) else if exist "%ROOT%\${ENV_ACTIVATE_REL}" (
    echo Activating: %ROOT%\${ENV_ACTIVATE_REL}
    call "%ROOT%\${ENV_ACTIVATE_REL}"
  ) else (
    echo [WARN] Env activation script not found: ${ENV_ACTIVATE_REL}
    echo        Continuing without activation.
  )
)

echo Using Python at:
where python

REM ====== sanity: accelerate available in THIS interpreter? ======
python -c "import accelerate,sys; sys.exit(0)" >nul 2>nul
if errorlevel 1 (
  if "${AUTO_INSTALL_ACCEL}"=="1" (
    echo Installing 'accelerate' into the active environment...
    python -m pip install --upgrade pip
    if errorlevel 1 ( echo [ERROR] Failed to upgrade pip. & exit /b 1 )
    python -m pip install accelerate
    if errorlevel 1 ( echo [ERROR] Failed to install accelerate. & exit /b 1 )
  ) else (
    echo [ERROR] Python cannot import 'accelerate' in this environment.
    echo         Fix with:  python -m pip install accelerate
    exit /b 1
  )
)

REM ====== sanity: paths exist? ======
if not exist "${SD_ENTRY}"        ( echo [ERROR] Missing training script: "%CD%\${SD_ENTRY}" & exit /b 2 )
if not exist "${UNET}"            ( echo [ERROR] Missing UNET: "${UNET}" & exit /b 3 )
if not exist "${CLIP_L}"          ( echo [ERROR] Missing CLIP_L: "${CLIP_L}" & exit /b 4 )
if not exist "${T5XXL}"           ( echo [ERROR] Missing T5XXL: "${T5XXL}" & exit /b 5 )
if not exist "${AE}"              ( echo [ERROR] Missing AE: "${AE}" & exit /b 6 )
if not exist "${DATASET_TOML}"    ( echo [ERROR] Missing dataset.toml: "${DATASET_TOML}" & exit /b 7 )
if not exist "${SAMPLE_PROMPTS}"  ( echo [ERROR] Missing sample_prompts.txt: "${SAMPLE_PROMPTS}" & exit /b 8 )

echo Starting FLUX LoRA training...
python -m accelerate.commands.launch ^
  --mixed_precision bf16 ^
  --num_cpu_threads_per_process 1 ^
  "${SD_ENTRY}" ^
  --pretrained_model_name_or_path "${UNET}" ^
  --clip_l "${CLIP_L}" ^
  --t5xxl "${T5XXL}" ^
  --ae "${AE}" ^
  --cache_latents_to_disk ^
  --save_model_as safetensors ^
  --sdpa --persistent_data_loader_workers ^
  --max_data_loader_n_workers ${MAX_WORKERS} ^
  --seed ${SEED} ^
  --gradient_checkpointing ^
  --save_precision bf16 ^
  --network_module networks.lora_flux ^
  --network_dim 64 ^
  --optimizer_type adafactor ^
  --optimizer_args "relative_step=False" "scale_parameter=False" "warmup_init=False" ^
  --split_mode ^
  --network_args "train_blocks=single" ^
  --lr_scheduler constant_with_warmup ^
  --max_grad_norm 0.0 ^
  --sample_prompts="${SAMPLE_PROMPTS}" ^
  --sample_every_n_steps 100 ^
  --learning_rate ${LR} ^
  --cache_text_encoder_outputs ^
  --cache_text_encoder_outputs_to_disk ^
  --max_train_epochs ${MAX_EPOCHS} ^
  --save_every_n_epochs ${SAVE_EVERY_EPOCH} ^
  --dataset_config "${DATASET_TOML}" ^
  --output_dir "${OUT_DIR}" ^
  --output_name ${OUT_NAME} ^
  --timestep_sampling shift ^
  --discrete_flow_shift 3.1582 ^
  --model_prediction_type raw ^
  --guidance_scale 1 ^
  --loss_type l2 ^
  ${FP8_FLAG}

if errorlevel 1 (
  echo [ERROR] Training failed with code %errorlevel%
  exit /b %errorlevel%
)

echo [OK] Training finished.
exit /b 0
)BATCH";

    Writer_::Writer w;
    if (!w.lines(B, V)) {
        std::cerr << "Placeholder replacement failed; check variables.\n";
        return false;
    }
    try {
        ensure_dir(bat_path.parent_path());
        w.save(bat_path);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------- Bring it together ----------
int main() {
    std::cout << "FluxGymScriptGen\n";

    // ==== EDIT THESE (your “profile”) ======================================
    const std::string FLUXGYM_ROOT = R"(F:\FluxGym\fluxgym)";             // your FluxGym root
    const std::string IMAGES_DIR = R"(C:\Users\Cosmos\Desktop\output\tmp\dataset)"; // your dataset folder
    const std::string TRIGGER = "my_trigger_world";                  // fallback class token
    const bool AUTORUN = true;                                // true => run the .bat via cmd
    const bool FORCE_FP8_T5 = false;                               // set true only if you actually have FP8 T5

    // Model files
    const std::string UNET = FLUXGYM_ROOT + R"(\models\unet\flux1-dev.sft)";
    const std::string CLIP = FLUXGYM_ROOT + R"(\models\clip\clip_l.safetensors)";
    const std::string T5 = FLUXGYM_ROOT + (FORCE_FP8_T5
        ? R"(\models\clip\t5xxl_fp8.safetensors)"
        : R"(\models\clip\t5xxl_fp16.safetensors)");
    const std::string AE = FLUXGYM_ROOT + R"(\models\vae\ae.sft)";

    // sd-scripts entry: keep relative (as FluxGym/your repo sets cwd) or give absolute.
    const std::string SD_SCRIPTS_ENTRY = R"(sd-scripts/flux_train_network.py)";

    // Output name defaults to images folder leaf (e.g., "dataset" -> outputs\dataset\...)
    const std::string OUT_NAME = leaf_name(IMAGES_DIR);
    const fs::path OUT_DIR = fs::path(FLUXGYM_ROOT) / "outputs" / OUT_NAME;

    const fs::path DATASET_TOML = OUT_DIR / "dataset.toml";
    const fs::path SAMPLE_PROMPTS = OUT_DIR / "sample_prompts.txt";
    const fs::path TRAIN_BAT = OUT_DIR / "train.bat";

    // Resolution/bucketing knobs
    const int MIN_RESO = 512;
    const int MAX_RESO = 1024;
    const int RES_X = 1024;
    const int RES_Y = 1024;
    const int REPEATS = 2;

    // ======================================================================

    if (!ensure_dir(OUT_DIR)) {
        std::cerr << "Failed to create output dir: " << OUT_DIR << "\n";
        return 1;
    }

    // Auto-detect if every image has a .txt caption (so we know whether to add class_tokens)
    const bool each_has_caption = has_caption_for_every_image(IMAGES_DIR);
    std::cout << "Captions: " << (each_has_caption ? "found for all images" : "not present for all images") << "\n";

    // 1) dataset.toml
    if (!write_dataset_toml(DATASET_TOML,
        IMAGES_DIR,
        TRIGGER,
        each_has_caption,
        MIN_RESO, MAX_RESO,
        RES_X, RES_Y,
        REPEATS)) {
        std::cerr << "Failed to write dataset.toml\n";
        return 1;
    }
    std::cout << "Wrote: " << DATASET_TOML << "\n";

    // 2) sample_prompts.txt
    if (!write_sample_prompts(SAMPLE_PROMPTS, TRIGGER)) {
        std::cerr << "Failed to write sample_prompts.txt\n";
        return 1;
    }
    std::cout << "Wrote: " << SAMPLE_PROMPTS << "\n";

    // 3) train.bat
    TrainFlags F;
    F.unet_path = UNET;
    F.clip_l = CLIP;
    F.t5xxl = T5;
    F.ae = AE;
    F.root_dir = FLUXGYM_ROOT;                      // project root for cd /d
    F.sd_scripts_entry = SD_SCRIPTS_ENTRY;
    F.dataset_toml = DATASET_TOML.string();
    F.sample_prompts = SAMPLE_PROMPTS.string();
    F.out_dir = OUT_DIR.string();
    F.out_name = OUT_NAME;
    F.fp8_base = FORCE_FP8_T5;
    F.max_epochs = 100;
    F.save_every_n_epochs = 1;
    F.lr = "8e-4";
    F.seed = 42;
    F.max_workers = 2;
    F.use_env_activation = true;                              // use the same env as your UI
    F.env_activate_rel = R"(env\Scripts\activate)";         // just like your working start script
    F.auto_install_accel = false;                             // set true to auto-install accelerate if missing

    if (!write_train_bat(TRAIN_BAT, F)) {
        std::cerr << "Failed to write train.bat\n";
        return 1;
    }
    std::cout << "Wrote: " << TRAIN_BAT << "\n";

    // 4) optionally run
    if (AUTORUN) {
        std::cout << "Launching training...\n";
        // Use cmd /c so ^ line continuations are handled correctly by cmd.exe
        std::string cmd = std::string("cmd /c \"") + TRAIN_BAT.string() + "\"";
        int rc = std::system(cmd.c_str());
        std::cout << "Training process returned " << rc << "\n";
    }

    std::cout << "All done.\n";
    return 0;
}
