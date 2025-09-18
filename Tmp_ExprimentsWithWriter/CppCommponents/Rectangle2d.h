#pragma once

namespace Rectangle2d_
{
	struct Rectangle2d
	{
		float x_min;
		float x_max;
		float y_min;
		float y_max;
	};

	void create(Rectangle2d& rectangle, float position_x, float position_y, float width, float height);
	void calculate_coordinates(const Rectangle2d& rectangle, float x, float y, float& out_x, float& out_y);
	
	bool isRectangleInside(const Rectangle2d& inner, const Rectangle2d& outer);

    bool areRectanglesIntersecting(const Rectangle2d& rect1, const Rectangle2d& rect2, Rectangle2d& intersection);
	bool checkCollision(const Rectangle2d& rect1, const Rectangle2d& rect2, bool& out_side_x_min, bool& out_side_x_max, bool& out_side_y_min, bool& out_side_y_max);

	void scaleRectangle(Rectangle2d& rect, float scaleX, float scaleY);

	void translate(Rectangle2d& rect, float x, float y);
}
