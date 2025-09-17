#include <iostream>

#include "Writer.h"

int main()
{
	std::cout << "Tmp_ExprimentsWithWriter\n";

	std::string filepath_to_images_folder = "C:/Users/Cosmos/Desktop/output/tmp/dataset/";

	std::string trigger_world = "my_trigger_world";

	bool each_image_has_txt_captions = false;

	// Creating toml
	{
		Writer_::Writer w;


		w.line("[general]");
		w.line("shuffle_caption = false                 # set true if you want tag shuffle");
		w.line("caption_extension = \".txt\"");
		w.line("keep_tokens = 1");
		w.blank();
		w.line("# Bucketing = automatic resize / crop per aspect ratio at load time");
		w.line("enable_bucket = true");
		w.line("bucket_reso_steps = 64");
		w.line("min_bucket_reso = 512");
		w.line("max_bucket_reso = 1024");
		w.line("bucket_no_upscale = true                  # don't enlarge small images (optional)");
		w.blank();
		w.line("[[datasets]]");
		w.line("# Either set one number(area target) or an explicit pair — both are supported.");
		w.line("# For clarity, use an explicit pair so \"max 1024×1024\" is obvious :");
		w.line("resolution = [1024, 1024]");
		w.line("batch_size = 1");
		w.line("keep_tokens = 1");
		w.blank();
		w.line("[[datasets.subsets]]");
		w.line("image_dir = \"${FOLDER_PATH}\"", { {"FOLDER_PATH", filepath_to_images_folder } });
		if (each_image_has_txt_captions)
		{
			w.line("class_tokens = \"{TRIGGER_WORLD}\"           # fallback caption; remove if you have per - image.txt", { {"TRIGGER_WORLD", trigger_world} });
		}
		else
		{

		}
		w.line("num_repeats = 2                        # simple weighting without duplicating files");

		w.save("C:/Users/Cosmos/Desktop/output/tmp/dataset.toml");
	}

	{
		Writer_::Writer w;
w.lines(R"BATCH(
accelerate launch ^
  --mixed_precision bf16 ^
  --num_cpu_threads_per_process 1 ^
  sd-scripts/flux_train_network.py ^
  --pretrained_model_name_or_path "F:\FluxGym\fluxgym\models\unet\flux1-dev.sft" ^
  --clip_l "F:\FluxGym\fluxgym\models\clip\clip_l.safetensors" ^
  --t5xxl "F:\FluxGym\fluxgym\models\clip\t5xxl_fp16.safetensors" ^
  --ae "F:\FluxGym\fluxgym\models\vae\ae.sft" ^
  --cache_latents_to_disk ^
  --save_model_as safetensors ^
  --sdpa --persistent_data_loader_workers ^
  --max_data_loader_n_workers 2 ^
  --seed 42 ^
  --gradient_checkpointing ^
  --save_precision bf16 ^
  --network_module networks.lora_flux ^
  --network_dim 64 ^
  --optimizer_type adafactor ^
  --optimizer_args "relative_step=False" "scale_parameter=False" "warmup_init=False" ^
  --split_mode max ^
  --network_args "train_blocks=single" ^
  --lr_scheduler constant_with_warmup ^
  --max_grad_norm 0.0 ^
  --sample_prompts="F:\FluxGym\fluxgym\outputs\NAMEOF_LORA\sample_prompts.txt" ^
  --sample_every_n_steps 100 ^
  --learning_rate 8e-4 ^
  --cache_text_encoder_outputs ^
  --cache_text_encoder_outputs_to_disk ^
  --max_train_epochs 100 ^
  --save_every_n_epochs 1 ^
  --dataset_config "F:\FluxGym\fluxgym\outputs\NAMEOF_LORA\dataset.toml" ^
  --output_dir "F:\FluxGym\fluxgym\outputs\NAMEOF_LORA" ^
  --output_name NAMEOF_LORA ^
  --timestep_sampling shift ^
  --discrete_flow_shift 3.1582 ^
  --model_prediction_type raw ^
  --guidance_scale 1 ^
  --loss_type l2
)BATCH", {});

	w.save("C:/Users/Cosmos/Desktop/output/tmp/run.bat");
	}
	

	
}


