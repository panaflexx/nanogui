#pragma once

#include <nanogui/widget.h>
#include <nanogui/opengl.h>
#include <cmath>

NAMESPACE_BEGIN(nanogui)


/** Returns the infinite Rect. */
#define IINFINITY 32767
static Vector4i inf() {
    return Vector4i(-IINFINITY, -IINFINITY, IINFINITY, IINFINITY);
}

/** Caches its children's draw() result to a framebuffer image.
When dirty, its children will be re-rendered on the next call to step().
*/
class FramebufferWidget : public Widget {
public:
	struct Internal;
	Internal* internal;

	bool dirty = true;
	bool bypassed = false;
	float oversample = 1.0;
	/** Redraw when the world offset of the FramebufferWidget changes its fractional value. */
	bool dirtyOnSubpixelChange = true;
	/** If finite, the maximum size of the framebuffer is the viewport expanded by this margin.
	The framebuffer is re-rendered when the viewport moves outside the margin.
	*/
	Vector2f viewportMargin = Vector2f(INFINITY, INFINITY);

	FramebufferWidget(Widget *parent);
	~FramebufferWidget();
	/** Requests to re-render children to the framebuffer on the next draw(). */
	void setDirty(bool dirty = true);
	int getImageHandle();
	NVGLUframebuffer* getFramebuffer();
	Vector2f getFramebufferSize();
	void deleteFramebuffer();

	/** Draws the framebuffer to the NanoVG scene, re-rendering it if necessary.
	*/
	virtual void draw(NVGcontext *ctx) override;
	/** Re-renders the framebuffer, re-creating it if necessary.
	Handles oversampling (if >1) by rendering to a temporary (larger) framebuffer and then downscaling it to the main persistent framebuffer.
	*/
	void render(NVGcontext *ctx, Vector2f scale = Vector2f(1, 1), Vector2f offsetF = Vector2f(0, 0), Vector4f clipBox = inf());
	/** Initializes the current GL context and draws children to it.
	*/
	void drawFramebuffer(NVGcontext *ctx);

	//void onDirty(const DirtyEvent& e) override;
	//void onContextCreate(const ContextCreateEvent& e) override;
	//void onContextDestroy(const ContextDestroyEvent& e) override;
};


NAMESPACE_END(nanogui)
