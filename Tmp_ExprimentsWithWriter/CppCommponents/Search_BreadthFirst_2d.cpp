#include "Search_BreadthFirst_2d.h"

#include <vector>
#include <iostream>
#include <queue>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <functional>

namespace Search_BreadthFirst_2d_
{
    // Definition of the non-member operator==
    bool operator==(const Point& lhs, const Point& rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
}

// Custom hash function for Point to use in unordered_set
namespace std
{
    template<> struct hash<Search_BreadthFirst_2d_::Point>
    {
        size_t operator()(const Search_BreadthFirst_2d_::Point& p) const
        {
            return hash<int>()(p.x) ^ (hash<int>()(p.y) << 1);
        }
    };
}

namespace Search_BreadthFirst_2d_
{
    BFSResult search
    (
        int grid_size_x, int grid_size_y,
        int start_x, int start_y,
        int target_x, int target_y,
        std::function<bool(int x, int y)> f_is_wall
    )
    {
        
        // Checks if (x, y) is within bounds and not a wall
        auto isValid = [&](int x, int y)
            {
                return x >= 0 && x < grid_size_x && y >= 0 && y < grid_size_y && f_is_wall(x, y) == false;
            };


        // For each pixel of image 
        // Was it visited before 


        std::queue<Point> q;           // BFS queue
        std::unordered_map<Point, Point> parent; // Tracks parent of each cell
        std::unordered_set<Point> visited; // Visited points

        // Start at (0, 0) if valid
        Point start(start_x, start_y);
        Point target(target_x, target_y);

        if (start == target)
        {
            if (!isValid(start.x, start.y))
            {
                BFSResult result;
                result.found = false;
                result.length = -1;
                result.path = { };

                return result;
            }
            else
            {
                BFSResult result;
                result.found = true;
                result.length = 0;
                result.path = { start};

                return result;
            }
            
        }

        if (isValid(start.x, start.y))
        {
            q.push(start);
            visited.insert(start);
            parent[start] = Point(-1, -1); // Start has no parent
        }

        // Movement directions: up, down, left, right
        const int dx[] = { -1, 1, 0, 0 };
        const int dy[] = { 0, 0, -1, 1 };

        bool found = false;

        while (!q.empty())
        {
            Point current = q.front();
            q.pop();

            // Early exist if target found
            if (current.x == target.x && current.y == target.y)
            {
                found = true;
                break;
            }

            // std::cout << "Visiting (" << current.x << ", " << current.y << ")\n";

            for (int i = 0; i < 4; ++i)
            {
                int new_x = current.x + dx[i];
                int new_y = current.y + dy[i];
                Point neighbor(new_x, new_y);

                if (isValid(new_x, new_y) && !visited.count(neighbor))
                {
                    visited.insert(neighbor);
                    parent[neighbor] = current; // Record parent
                    q.push(neighbor);
                }
            }
        }

        // Reconstruct the shortest path (if found)
        if (found)
        {
            std::vector<Point> path;
            Point current = target;
            while (current.x != -1 || current.y != -1) {
                path.push_back(current);
                current = parent[current];
            }

            std::reverse(path.begin(), path.end());
            // std::cout << "Shortest path (length " << path.size() - 1 << "):\n";
           
            /*
            for (auto& p : path)
            {
                std::cout << "(" << p.x << ", " << p.y << ") ";
            }
            */

            BFSResult result;
            result.found = true;
            result.length = path.size() - 1;
            result.path = std::move(path);

            return result;
        }
        else
        {
            // std::cout << "Target unreachable!\n";

            std::vector<Point> path;
            
            BFSResult result;
            result.found = false;
            result.length = -1;
            result.path = std::move(path);

            return result;
        }

    }

    void display_result(const BFSResult& results)
    {
        std::cout << "found : " << results.found << "\n";
        std::cout << "length : " << results.length << "\n";
        std::cout << "-- shortest path --\n";
        for (int i = 0; i < results.path.size(); i++)
        {
            std::cout << "x : " << results.path[i].x << " y : " << results.path[i].y << "\n";
        }
    }
    

}