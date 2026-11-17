#pragma once

#include <algorithm>

namespace zoominator {

/* Edge zone ("camera dead zone") for cursor-follow.
 *
 * The zoomed view is tracked by its centre. While the cursor stays inside the inner rectangle of the view the
 * centre does not move; once the cursor enters the edge zone - the outer `zoneRatio` of the view on each side -
 * the centre gives way by exactly the overshoot, so the cursor rides the zone boundary and the view stops as
 * soon as the cursor does.
 *
 * Kept free of OBS types so it can be unit tested alone. */

/* One axis: the view centre after the cursor has moved. `viewSize` is the extent of the zoomed view along that
 * axis. */
inline double edge_zone_center(double center, double cursor, double viewSize, double zoneRatio)
{
	const double inner = std::max(0.0, 0.5 - zoneRatio) * viewSize;
	return std::clamp(center, cursor - inner, cursor + inner);
}

/* The view centre after a zoom change that keeps the cursor on the same spot of the view, i.e. zooms about the
 * cursor instead of about the view centre. */
inline double edge_zone_rezoom(double center, double cursor, double zoomFrom, double zoomTo)
{
	return cursor - (cursor - center) * (zoomFrom / zoomTo);
}

/* The follow focal point f and the centre V of the visible region are related by V = f + (c - f) * k, where c is
 * the canvas centre and k depends on how the scene is anchored (see focal_view_factor). */
inline double focal_view_factor(double zoom, double zoomRamp, bool centerAnchor)
{
	/* Centre anchor: the anchor slides from f to c with the zoom ramp, so V = f at the end of the ramp.
	 * Anchor on f itself: the point at the canvas centre is f + (c - f) / z. */
	return centerAnchor ? (1.0 - zoomRamp) / zoom : 1.0 / zoom;
}

inline double focal_to_view_center(double focal, double canvasCenter, double k)
{
	return focal * (1.0 - k) + canvasCenter * k;
}

inline double view_center_to_focal(double center, double canvasCenter, double k)
{
	return (center - canvasCenter * k) / (1.0 - k);
}

} // namespace zoominator
