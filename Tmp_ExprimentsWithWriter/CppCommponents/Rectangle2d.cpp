#include "Rectangle2d.h"

#include <algorithm>

namespace Rectangle2d_
{
	void create(Rectangle2d& rectangle, float position_x, float position_y, float width, float height)
	{
		rectangle.x_min = position_x - width * 0.5f;
		rectangle.x_max = position_x + width * 0.5f;

		rectangle.y_min = position_y - width * 0.5f;
		rectangle.y_max = position_y + width * 0.5f;
	}

	void calculate_coordinates(const Rectangle2d& rectangle, float x, float y, float& out_x, float& out_y)
	{
		float rectangle_width = rectangle.x_max - rectangle.x_min;
		float rectangle_height = rectangle.y_max - rectangle.y_min;
		out_x = rectangle.x_min + x * rectangle_width;
		out_y = rectangle.y_min + y * rectangle_height;
	}

	bool isRectangleInside(const Rectangle2d& inner, const Rectangle2d& outer)
	{
		return (inner.x_min >= outer.x_min &&
			inner.x_max <= outer.x_max &&
			inner.y_min >= outer.y_min &&
			inner.y_max <= outer.y_max);
	}

	bool areRectanglesIntersecting(const Rectangle2d& rect1, const Rectangle2d& rect2, Rectangle2d& intersection)
	{
		// Calculate the intersection boundaries
		float x_min_intersection = std::max(rect1.x_min, rect2.x_min);
		float x_max_intersection = std::min(rect1.x_max, rect2.x_max);
		float y_min_intersection = std::max(rect1.y_min, rect2.y_min);
		float y_max_intersection = std::min(rect1.y_max, rect2.y_max);

		// Check if there is an intersection
		if (x_min_intersection < x_max_intersection && y_min_intersection < y_max_intersection) {
			// Set the intersection rectangle
			intersection.x_min = x_min_intersection;
			intersection.x_max = x_max_intersection;
			intersection.y_min = y_min_intersection;
			intersection.y_max = y_max_intersection;
			return true;
		}
		else {
			// No intersection
			return false;
		}
	}

	// Function to check for collision and return the sides of collision
	bool checkCollision(const Rectangle2d& rect1, const Rectangle2d& rect2, bool& out_side_x_min, bool& out_side_x_max, bool& out_side_y_min, bool& out_side_y_max)
	{
		out_side_x_min = false;
		out_side_x_max = false;
		out_side_y_min = false;
		out_side_y_max = false;

		// Check if there is a collision
		bool isColliding = !(rect1.x_max < rect2.x_min || rect1.x_min > rect2.x_max ||
			rect1.y_max < rect2.y_min || rect1.y_min > rect2.y_max);

		if (isColliding)
		{
			// Determine the sides of collision
			if (rect1.x_max > rect2.x_min && rect1.x_min < rect2.x_min)
			{
				// collisionSides.push_back("side_x_min");
				out_side_x_min = true;
				
			}
			if (rect1.x_min < rect2.x_max && rect1.x_max > rect2.x_max)
			{
				// collisionSides.push_back("side_x_max");
				out_side_x_max = true;
			}
			if (rect1.y_max > rect2.y_min && rect1.y_min < rect2.y_min)
			{
				// collisionSides.push_back("side_y_min");
				out_side_y_min = true;
			}
			if (rect1.y_min < rect2.y_max && rect1.y_max > rect2.y_max)
			{
				// collisionSides.push_back("side_y_max");
				out_side_y_max = true;
			}


			return true;
		}

		return false;
	}

	void scaleRectangle(Rectangle2d& rect, float scaleX, float scaleY)
	{
		float width = rect.x_max - rect.x_min;
		float height = rect.y_max - rect.y_min;

		float centerX = rect.x_min + width / 2.0f;
		float centerY = rect.y_min + height / 2.0f;

		float newWidth = width * scaleX;
		float newHeight = height * scaleY;

		rect.x_min = centerX - newWidth / 2.0f;
		rect.x_max = centerX + newWidth / 2.0f;
		rect.y_min = centerY - newHeight / 2.0f;
		rect.y_max = centerY + newHeight / 2.0f;
	}

	void translate(Rectangle2d& rect, float x, float y)
	{
		rect.x_min += x;
		rect.x_max += x;
		
		rect.y_min += y;
		rect.y_max += y;
	}

}
