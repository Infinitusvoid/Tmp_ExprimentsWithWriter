#pragma once

#include "CppCommponents/Random.h"

#include <iostream>

#include "Writer.h"

#include <chrono>
#include <thread>
using namespace std::chrono_literals;




void generate_shader()
{
	

	Writer_::Writer w;

	// Generate Header
	{
		w.line("#version 450 core");
		w.line("layout(location = 0) in vec3 aPos;");
		w.line("layout(location = 1) in vec2 aTexCoord;");
		w.blank();


		w.comment("outputs to fragment");
		w.line("out vec2 TexCoord;");
		w.line("out vec3 color_vs;");
		w.line("out vec3 vWorldPos;");
		w.line("out vec3 vNormal;");
		w.blank();

		w.comment("uniforms");
		w.line("uniform mat4 model;       // can be identity");
		w.line("uniform mat4 view;");
		w.line("uniform mat4 projection;");
		w.line("uniform ivec3 uGrid;      // number of instances along X,Y,Z (instanceCount = X*Y*Z)");
		w.line("uniform float uSpacing;   // distance between grid cells"); // we are not using this
		w.line("uniform vec3  uOrigin;    // base world offset");
		w.line("uniform vec3  uScaleMin;  // min scale per axis");
		w.line("uniform vec3  uScaleMax;  // max scale per axis");
		w.line("uniform float uTime;      // time (seconds)");
		w.line("uniform float uRotSpeed;  // radians/sec");
		w.line("uniform uint  uSeed;      // global random seed");
		w.blank();
		w.line("uniform uint uDrawcallNumber;");
		w.line("uniform vec3 uCameraPos;");
		w.line("uniform float u0, u1, u2, u3, u4, u5, u6, u7, u8, u9;");
		w.blank();

		w.comment("// ---------- Constants & tiny helpers ----------");
		w.line("const float PI = 3.1415926535897932384626433832795;");
		w.line("const float TAU = 6.2831853071795864769252867665590;");
		w.blank();

		w.line("float saturate(float x) { return clamp(x, 0.0, 1.0); }");
		w.blank();

		w.lines(R"GLSL(
uint pcg_hash(uint x) {
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return x;
}
)GLSL", {});
		w.blank();

		w.line("float rand01(inout uint s) { s = pcg_hash(s); return float(s) * (1.0 / 4294967295.0); }");
		w.blank();

		w.lines(R"GLSL(
vec3 spherical01(float r, float theta01, float phi01) {
    float theta = theta01 * TAU; // azimuth
    float phi = phi01 * PI;   // polar
    float sphi = sin(phi);
    return vec3(r * sphi * cos(theta), r * cos(phi), r * sphi * sin(theta));
}
)GLSL", {});
		w.blank();

		w.lines(R"GLSL(
mat3 axisAngleToMat3(vec3 axis, float a) {
    float c = cos(a), s = sin(a);
    vec3 t = (1.0 - c) * axis;
    return mat3(
        t.x * axis.x + c, t.x * axis.y - s * axis.z, t.x * axis.z + s * axis.y,
        t.y * axis.x + s * axis.z, t.y * axis.y + c, t.y * axis.z - s * axis.x,
        t.z * axis.x - s * axis.y, t.z * axis.y + s * axis.x, t.z * axis.z + c
    );
}
)GLSL", {});
		w.blank();

		w.lines(R"GLSL(
// Axis-aligned cube face normal from aPos (local space)
vec3 localCubeFaceNormal(vec3 p) {
    vec3 ap = abs(p);
    if (ap.x >= ap.y && ap.x >= ap.z) return vec3(sign(p.x), 0.0, 0.0);
    if (ap.y >= ap.x && ap.y >= ap.z) return vec3(0.0, sign(p.y), 0.0);
    return vec3(0.0, 0.0, sign(p.z));
}
)GLSL", {});
		w.blank();
	}

	

	w.lines(R"GLSL(
// 0 to 1
float f_periodic_0(float x)
{
    return 2.0 * abs(fract(x + 0.5) - 0.5);
}

// Square Wave 
float f_periodic_1(float x)
{
    return  floor(x) - floor(x - 0.5);
}

// The Bouncing Ball (Parabolic Arches)
float f_periodic_2(float x)
{
    return 4 * fract(x) * (1 - fract(x));
}

float f_periodic_3(float x)
{
    return exp(-30 * ((fract(x + 0.5) - 0.5) * (fract(x + 0.5) - 0.5)));
}

float f_periodic_4(float x)
{
    return abs(0.7 * cos(2 * PI * x) + 0.3 * cos(6 * PI * x)) * (-1.0) + 1.0;
}

float f_periodic_5(float x)
{ 
    return 1.0 - abs(round(10 * fract(x)) / 10 - 0.5) * 2.0;
}

float f_periodic_6(float x)
{
    return sqrt(4 * fract(x) * (1 - fract(x)));
}

float f_periodic_7(float x)
{
    return sin(5 * PI * fract(x)) * (1 - fract(x));
}

// 1) Raised-cosine (Hann) arch — smooth & band-limited-ish
float f_periodic_8(float x)
{
    return 0.5 - 0.5 * cos(TAU * x);               // 0 at integers, 1 at half-integers
}

float f_periodic_9(float x)
{
    return pow(2.0 * abs(fract(x + 0.5) - 0.5), 1.5);
}

float f_periodic_10(float x)
{
    return (abs(1.0 / (1.0 + exp(-6.0 * sin(TAU * x))) - 0.5)) * 2.0 * 2.0 * abs(fract(x + 0.5) - 0.5);
}

float f_periodic_11(float x)
{
    return fract(x) * fract(x) * (3.0 - 2.0 * fract(x)) * 2.0 * abs(fract(x + 0.5) - 0.5) * 1.9;
}

float f_adjust_to_two_pi(float x)
{
    return x * (1.0 / TAU);
}
)GLSL", {});
	w.blank();

	w.line("void main()");
	w.open("{");

	w.line("int id = gl_InstanceID;");
	w.blank();

	w.line("id =  id + (uGrid.x * uGrid.y * uGrid.z) * int(uDrawcallNumber);");
	w.blank();

	w.lines(R"GLSL(
// Per-instance randomness
    uint s0 = uSeed + uint(id + 0);
    uint s1 = uSeed + uint(id + 42);
    uint s2 = uSeed + uint(id + 142);
    float rnd_x = rand01(s0);
    float rnd_y = rand01(s1);
    float rnd_z = rand01(s2);

    // The instancd cube rotation randomization
    uint s0_rot_x = uSeed + uint(id + 2431);
    uint s1_rot_y = uSeed + uint(id + 4412);
    uint s2_rot_y = uSeed + uint(id + 1234);
    uint s3_rot_angle = uSeed + uint(id + 2332);
    float rnd_cube_rotation_x = rand01(s0_rot_x);
    float rnd_cube_rotation_y = rand01(s1_rot_y);
    float rnd_cube_rotation_z = rand01(s2_rot_y);
    float rnd_cube_rotation_angle = rand01(s3_rot_angle);
)GLSL", {});
	w.blank();

	{
		class Wave
		{
		public:
			enum class Direction
			{
				X,
				Y
			};

			Direction direction = Direction::X;

			int frequency_index = 1;
			float offset = 0.0f;
			float amplitude = 1.0f;
			float time_multiplier = 0.0;
			int function_to_use = 0;

			void write(Writer_::Writer& w, int index, std::string name)
			{
				std::string direction_txt = "x";
				if (direction == Direction::Y)
				{
					direction_txt = "y";
				}

				w.comment("${NAME} ${DIRECTION} ${INDEX} ", { {"NAME", name}, {"DIRECTION", direction_txt}, {"INDEX", std::to_string(index)} });
				w.linef("int {}_{}_{}_frequency = int({});", name, index, direction_txt, frequency_index);
				w.linef("float {}_{}_{}_offset = float({});", name, index, direction_txt, offset);
				w.linef("float {}_{}_{}_amplitude = float({});", name, index, direction_txt, amplitude);
				w.linef("float {}_{}_{}_t = uTime * float({});", name, index, direction_txt, float(this->time_multiplier));
				w.blank();
			}

			static void generate_waves(std::vector<Wave>& waves, int num)
			{
				for (int i = 0; i < num; i++)
				{
					Wave wave;
					wave.frequency_index = Random::random_int(1, 10);
					wave.offset = Random::generate_random_float_minus_one_to_plus_one() * 10.0f;
					wave.amplitude = Random::generate_random_float_minus_one_to_plus_one() * 0.37f * (1.0f / float(i + 1));
					wave.time_multiplier = Random::generate_random_float_minus_one_to_plus_one() * 0.01f * (1.0f / float(i * i + 1));
					wave.function_to_use = Random::random_int(0, 10);

					if (Random::generate_random_float_0_to_1() > 0.5f)
					{
						wave.direction = Wave::Direction::X;
					}
					else
					{
						wave.direction = Wave::Direction::Y;
					}

					waves.push_back(wave);
				}
			}

			static void normalize_amplitude(std::vector<Wave>& waves)
			{
				float total_amplitude = 0.0;
				for (int i = 0; i < waves.size(); i++)
				{
					total_amplitude += waves[i].amplitude;
				}

				float factor = 1.0 / total_amplitude;

				if (std::abs(total_amplitude) > 1.0)
				{
					for (int i = 0; i < waves.size(); i++)
					{
						waves[i].amplitude = factor * waves[i].amplitude;
					}
				}
			}


			static void write(Writer_::Writer& w, std::vector<Wave>& waves, std::string name)
			{
				{
					for (int i = 0; i < waves.size(); i++)
					{
						waves[i].write(w, i, name);
					}

				}

				{
					w.blank();
					w.line("float ${NAME} = 0.0f;", { {"NAME", name} });



					for (int i = 0; i < waves.size(); i++)
					{
						int function_to_use = waves[i].function_to_use;



						w.line
						(
							"${NAME} += ${NAME}_${INDEX}_${DIRECTION}_amplitude * f_periodic_${PERIODIC_FUNCTION}(f_adjust_to_two_pi(${NAME}_${INDEX}_${DIRECTION}_offset + ${X_OR_Y} * TAU * ${NAME}_${INDEX}_${DIRECTION}_frequency + ${NAME}_${INDEX}_${DIRECTION}_t * uTime));",
							{
								{"NAME", name},
								{"INDEX", std::to_string(i)},
								{"DIRECTION", ((waves[i].direction == Wave::Direction::X) ? "x" : "y")},
								{"PERIODIC_FUNCTION", std::to_string(function_to_use)},
								{"X_OR_Y", ((waves[i].direction == Wave::Direction::X) ? "rnd_x" : "rnd_y")  }
							}
						);
					}


					w.blank();
					w.linef("{} *= float({});", name, 0.2f);

				}
			}
		};



		std::string name_0 = "first_wave";
		std::string name_1 = "second_wave";


		{
			std::vector<Wave> waves;

			Wave::generate_waves(waves, 20);
			Wave::normalize_amplitude(waves);

			Wave::write(w, waves, name_0);

			w.blank();
		}


		{
			std::vector<Wave> waves;

			Wave::generate_waves(waves, 20);
			Wave::normalize_amplitude(waves);

			Wave::write(w, waves, name_1);

			w.blank();

		}


		{
			w.line("float f_0 = fract(uTime * 0.1);");
			w.line("float f_1 = 1.0 - f_0;");
			w.blank();
			w.line("float w = f_1 * ${NAME_0} + f_0 * ${NAME_1};", { {"NAME_0", name_0} , {"NAME_1", name_1} });


		}

	}


	w.blank();

	w.line("float radius = 0.2 + w;");

	w.lines(R"GLSL(
// Sphere
    vec3 sphere_position = spherical01(radius, rnd_x, rnd_y);
    float px = sphere_position.x;
    float py = sphere_position.y;
    float pz = sphere_position.z;

    float color_r = 0.01;
    float color_g = 0.01;
    float color_b = 0.01;

    
    // Instances Cube Scale
    float scale_cube = 0.001;
    vec3  pos = vec3(px, pz, py);
    vec3  scale = vec3(scale_cube, scale_cube, scale_cube);


    // Whole object rotation

    vec3 rotation_axis = vec3(0.0, 1.0, 0.0);
    float rotation_angle = uTime; // using uTime will not be wise after we will be interpolating between two values

    // Whole object scale
    vec3 scale_object = vec3(1.0, 1.0, 1.0);

    

    vec4 new_position = vec4(vec3(pos), 1.0);

    if (true) {

        uint s0_instance_0 = uSeed + uint(uint(u0 * 1000.0f));
        uint s0_instance_1 = uSeed + uint(uint(u0 * 1421.0f));
        float rnd_instance_0 = rand01(s0_instance_0);
        float rnd_instance_1 = rand01(s0_instance_1);

        uint s0_instance_x_scale = uSeed + uint(uint(u0 * 14024.0f));
        uint s0_instance_y_scale = uSeed + uint(uint(u0 * 15214.0f));
        uint s0_instance_z_scale = uSeed + uint(uint(u0 * 14215.0f));
        float rnd_instance_scale_x = rand01(s0_instance_x_scale);
        float rnd_instance_scale_y = rand01(s0_instance_y_scale);
        float rnd_instance_scale_z = rand01(s0_instance_z_scale);

        // Rotation
        // vec3 axis = normalize(vec3(0.0, 1.0, 1.0));
        vec3 axis = normalize(rotation_axis);
        // float angle = uTime;
        float angle = rotation_angle;
        mat3 R3 = axisAngleToMat3(axis, angle);
        mat4 R = mat4(vec4(R3[0], 0.0), vec4(R3[1], 0.0), vec4(R3[2], 0.0), vec4(0, 0, 0, 1));

        // Translation
        mat4 T = mat4(1.0);
        vec3 offset = vec3(sin(uTime + rnd_instance_0 * 10.0) * 10.0, sin(uTime + rnd_instance_1 * 0.0) * 10.0, 0.0);
        offset = vec3(0.5, 0.5, 0.5);
        T[3] = vec4(offset, 1.0);



        // Scale
        mat4 S = mat4(1.0);
        S[0][0] = scale_object.x;
        S[1][1] = scale_object.y;
        S[2][2] = scale_object.z;

        new_position = T * R * S * new_position;
    }



    pos = new_position.xyz;
    





    
    // Per-instance tint (kept neutral here)
    color_vs = vec3(color_r, color_g, color_b);

    // Build TRS
    mat4 T = mat4(1.0); T[3] = vec4(pos, 1.0);
    vec3 axis = normalize(vec3(rnd_cube_rotation_x, rnd_cube_rotation_y, rnd_cube_rotation_z));
    float angle = rnd_cube_rotation_angle;//uTime * 0.0;
    mat3 R3 = axisAngleToMat3(axis, angle);
    mat4 R = mat4(vec4(R3[0], 0.0), vec4(R3[1], 0.0), vec4(R3[2], 0.0), vec4(0, 0, 0, 1));
    mat4 S = mat4(1.0); S[0][0] = scale.x; S[1][1] = scale.y; S[2][2] = scale.z;

    mat4 instanceModel = T * R * S;
    mat4 M = model * instanceModel;

    // World-space position (for lighting)
    vec4 wp = M * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;

    // World-space normal:
    // Fast path (assumes uniform scale): rotate the face normal by model rotation and R3.
    // If you later use non-uniform model scale, switch to normal matrix:
    //   mat3 N = transpose(inverse(mat3(M)));
    //   vNormal = normalize(N * nLocal);
    vec3 nLocal = localCubeFaceNormal(aPos);
    vNormal = normalize(mat3(model) * (R3 * nLocal)); // uniform-scale assumption

    // Clip-space position and UV
    gl_Position = projection * view * wp;
    TexCoord = aTexCoord;


    // World position color

    // float world_x = wp.x;
    // float world_y = wp.y;
    // float world_z = wp.z;
    // color_vs = vec3(sin(world_x * 10.0), sin(world_y * 10.0), sin(world_z * 10.0)) * vec3(0.01, 0.01, 0.01);
)GLSL", {});

	w.close("}");


	w.save("C:/Users/Cosmos/Documents/GitHub/Tmp/Tmp/shaders/vertex_9.glsl");
}

int main()
{
	std::cout << "LetGenerateShadersNicely\n";

	auto next = std::chrono::steady_clock::now();

	while (true)
	{
		next += 4s;

		generate_shader();

		std::this_thread::sleep_until(next);
		std::cout << "Shader generated\n";
	}

	generate_shader();


	return 0;
}