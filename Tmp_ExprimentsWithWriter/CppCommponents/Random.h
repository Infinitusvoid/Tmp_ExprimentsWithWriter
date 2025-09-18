#pragma once

#include <vector>
#include <random>

namespace Random
{
	// === Random Engine Setup ===
	// A thread-local Mersenne Twister engine, seeded once per program.
	inline std::mt19937& engine() {
		static thread_local std::mt19937 eng{ static_cast<unsigned>(std::time(nullptr)) };
		return eng;
	}

	// === Seed Control ===
	// Resets the engine seed to the current time (call once if reproducibility is not needed)
	inline void reset_seed_to_current_time() {
		engine().seed(static_cast<unsigned>(std::time(nullptr)));
	}

	// Sets the engine seed explicitly (for reproducibility)
	inline void set_seed(unsigned int seed) {
    		engine().seed(seed);
	}	

	// === Existing-like Functions (Modernized) ===

	// Generates a random float in [0.0, 1.0]
	inline float generate_random_float_0_to_1() {
		std::uniform_real_distribution<float> dist(0.0f, 1.0f);
		return dist(engine());
	}

	// Generates a random float in [-1.0, +1.0]
	inline float generate_random_float_minus_one_to_plus_one() {
		std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
		return dist(engine());
	}

	// === New Utility Functions ===

	// Generates a random integer in the inclusive range [min, max]
	inline int random_int(int min, int max) {
		std::uniform_int_distribution<int> dist(min, max);
		return dist(engine());
	}

	// Generates a random boolean value (true or false)
	inline bool random_bool() {
		std::bernoulli_distribution dist(0.5);
		return dist(engine());
	}

	// Generates a random float in the inclusive range [min, max]
	inline float random_float(float min, float max) {
		std::uniform_real_distribution<float> dist(min, max);
		return dist(engine());
	}

	// Picks a random element from a const std::vector<T>&
	template<typename T>
	inline const T& random_element(const std::vector<T>& vec) {
		auto idx = random_int(0, static_cast<int>(vec.size()) - 1);
		return vec.at(idx);
	}

	// Picks a random element from a std::vector<T>& (allows modification)
	template<typename T>
	inline T& random_element(std::vector<T>& vec) {
		auto idx = random_int(0, static_cast<int>(vec.size()) - 1);
		return vec.at(idx);
	}

	// Shuffles the contents of a std::vector<T> in-place
	template<typename T>
	inline void shuffle_vector(std::vector<T>& vec)
	{
		std::shuffle(vec.begin(), vec.end(), engine());
	}

	// Returns a random sample of `count` elements from `vec`. If `unique` is true,
	// elements will not repeat (count must be <= vec.size()). Otherwise, duplicates are allowed.
	template<typename T>
	inline std::vector<T> random_sample(const std::vector<T>& vec, size_t count, bool unique = true) {
		std::vector<T> result;
		result.reserve(count);
		if (unique) {
			std::vector<T> temp = vec;
			shuffle_vector(temp);
			size_t take = std::min(count, temp.size());
			result.insert(result.end(), temp.begin(), temp.begin() + take);
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				result.push_back(random_element(vec));
			}
		}
		return result;
	}
}
