#pragma once

#include <functional>
#include <vector>

namespace Search_BreadthFirst_2d_
{
    struct Point
    {
        int x, y;
    };

    struct BFSResult
    {
        bool found;
        int length;              // -1 if not found
        std::vector<Point> path;
    };

    BFSResult search
    (
        int grid_size_x, int grid_size_y,
        int start_x, int start_y,
        int target_x, int target_y,
        std::function<bool(int x, int y)> f_is_wall
    );


    void display_result(const BFSResult& results);

}
