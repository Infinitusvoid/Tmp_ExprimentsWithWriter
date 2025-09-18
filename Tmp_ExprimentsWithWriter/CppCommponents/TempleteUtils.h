#pragma once

template <typename Container, typename Function>
void for_each_in_container(Container& container, Function func) {
	for (auto& element : container) {
		func(element);
	}
}
