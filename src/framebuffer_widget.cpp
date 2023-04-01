/*
    nanogui/framebuffer_widget.h -- Buffered widget

    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
/** \file */


#include <vector>
#include <algorithm>
#include <nanogui/object.h>
#include <nanogui/widget.h>
#include <nanogui/layout.h>
#include <nanogui/theme.h>
#include <nanogui/window.h>
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/framebuffer_widget.h>



NAMESPACE_BEGIN(nanogui)

static int FramebufferWidget_totalPixels = 0;
#define VEC_ARGS(v) (v).x(), (v).y()


struct FramebufferWidget::Internal {
	NVGLUframebuffer* fb = NULL;

	/** Pixel dimensions of the allocated framebuffer */
	Vector2f fbSize;
	/** Bounding box in world coordinates of where the framebuffer should be painted.
	Always has integer coordinates so that blitting framebuffers is pixel-perfect.
	*/
	Vector4i fbBox;
	/** Framebuffer's scale relative to the world */
	Vector2f fbScale;
	/** Framebuffer's subpixel offset relative to fbBox in world coordinates */
	Vector2f fbOffsetF;
	/** Local box where framebuffer content is valid.
	*/
	Vector4i fbClipBox = inf();
};


FramebufferWidget::FramebufferWidget(Widget *parent) : Widget(parent) {
	internal = new Internal;
}


FramebufferWidget::~FramebufferWidget() {
	deleteFramebuffer();
	delete internal;
}


void FramebufferWidget::setDirty(bool dirty) {
	this->dirty = dirty;
}


int FramebufferWidget::getImageHandle() {
	if (!internal->fb)
		return -1;
	return internal->fb->image;
}


NVGLUframebuffer* FramebufferWidget::getFramebuffer() {
	return internal->fb;
}


Vector2f FramebufferWidget::getFramebufferSize() {
	return internal->fbSize;
}


void FramebufferWidget::deleteFramebuffer() {
	if (!internal->fb)
		return;

	// If the framebuffer exists, the Window should exist.
	assert(APP->window);

	nvgluDeleteFramebuffer(internal->fb);
	internal->fb = NULL;

	FramebufferWidget_totalPixels -= (internal->fbSize[0] * internal->fbSize[1]);
}



void FramebufferWidget::draw(NVGcontext *ctx) {
	// Draw directly if bypassed or already drawing in a framebuffer
	if (bypassed) {
		Widget::draw(ctx);
		return;
	}

	// Get world transform
	float xform[6] = {};
	nvgCurrentTransform(ctx, xform);
	// Skew and rotate is not supported
    /*
	if (!math::isNear(xform[1], 0.f) || !math::isNear(xform[2], 0.f)) {
		WARN("Skew and rotation detected but not supported in FramebufferWidget.");
		return;
	}
    */

	// Extract scale and offset from world transform
	Vector2f scale = Vector2f(xform[0], xform[3]);
	Vector2f offset = Vector2f(xform[4], xform[5]);
	Vector2f offsetI = Vector2f(floor(offset[0]), floor(offset[1]));
	Vector2f offsetF = offset - offsetI;
    Vector4f clipBox = Vector4f(-INFINITY, -INFINITY, INFINITY, INFINITY);

    /*
	// Re-render if drawing to a new subpixel location.
	// Anything less than 0.1 pixels isn't noticeable.
	Vector2f offsetFDelta = offsetF.minus(internal->fbOffsetF);
	if (dirtyOnSubpixelChange && APP->window->fbDirtyOnSubpixelChange() && offsetFDelta.square() >= std::pow(0.1f, 2)) {
		// DEBUG("%p dirty subpixel (%f, %f) (%f, %f)", this, VEC_ARGS(offsetF), VEC_ARGS(internal->fbOffsetF));
		setDirty();
	}
	// Re-render if rescaled.
	else if (!scale.equals(internal->fbScale)) {
		// DEBUG("%p dirty scale", this);
		setDirty();
	}
	// Re-render if viewport is outside framebuffer's clipbox when it was rendered.
	else if (!internal->fbClipBox.contains(args.clipBox)) {
		setDirty();
	}
    */

	if (dirty) {
		// Render only if there is frame time remaining (to avoid lagging frames significantly), or if it's one of the first framebuffers this frame (to avoid framebuffers from never rendering).
        /*
		const int minCount = 1;
		const double minRemaining = -1 / 60.0;
		int count = ++APP->window->fbCount();
		double remaining = APP->window->getFrameDurationRemaining();
		if (count <= minCount || remaining > minRemaining) {
        */
			render(ctx, scale, offsetF, clipBox);
		//}
	}

	if (!internal->fb)
		return;

	// Draw framebuffer image, using world coordinates
	nvgSave(ctx);
	nvgResetTransform(ctx);

	Vector2f scaleRatio = scale / internal->fbScale;
	// DEBUG("%f %f %f %f", scaleRatio.x, scaleRatio.y, offsetF.x, offsetF.y);

	// DEBUG("%f %f %f %f, %f %f", RECT_ARGS(internal->fbBox), VEC_ARGS(internal->fbSize));
	// DEBUG("offsetI (%f, %f) fbBox (%f, %f; %f, %f)", VEC_ARGS(offsetI), RECT_ARGS(internal->fbBox));
	nvgBeginPath(ctx);
	nvgRect(ctx,
		offsetI.x() + internal->fbBox.x() * scaleRatio.x(),
		offsetI.y() + internal->fbBox.y() * scaleRatio.y(),
		internal->fbBox.z() * scaleRatio.x(),
		internal->fbBox.w() * scaleRatio.y());
	NVGpaint paint = nvgImagePattern(ctx,
		offsetI.x() + internal->fbBox.x() * scaleRatio.x(),
		offsetI.y() + internal->fbBox.y() * scaleRatio.y(),
		internal->fbBox.z() * scaleRatio.x(),
		internal->fbBox.w() * scaleRatio.y(),
		0.0, internal->fb->image, 1.0);
	nvgFillPaint(ctx, paint);
	nvgFill(ctx);

	// For debugging the bounding box of the framebuffer
	// nvgStrokeWidth(ctx, 2.0);
	// nvgStrokeColor(ctx, nvgRGBAf(1, 1, 0, 0.5));
	// nvgStroke(ctx);

	nvgRestore(ctx);
}


void FramebufferWidget::render(NVGcontext* vg, Vector2f scale, Vector2f offsetF, Vector4f clipBox) {
	// In case we fail drawing the framebuffer, don't try again the next frame, so reset `dirty` here.
	dirty = false;
	//NVGcontext* vg = APP->window->vg;
	//NVGcontext* fbVg = APP->window->fbVg;

	internal->fbScale = scale;
	internal->fbOffsetF = offsetF;

	Vector4i localBox = Vector4f(m_pos.x(),m_pos.y(), m_size.x(), m_size.y());
	/*
    if (!children().empty()) {
		localBox = getVisibleChildrenBoundingBox();
	}
    */

	// Intersect local box with viewport if viewportMargin is set
    /*
	internal->fbClipBox = clipBox.grow(viewportMargin);
	if (internal->fbClipBox.size.isFinite()) {
		localBox = localBox.intersect(internal->fbClipBox);
	}
    */

	// DEBUG("rendering FramebufferWidget localBox (%f, %f; %f, %f) fbOffset (%f, %f) fbScale (%f, %f)", RECT_ARGS(localBox), VEC_ARGS(internal->fbOffsetF), VEC_ARGS(internal->fbScale));
	// Transform to world coordinates, then expand to nearest integer coordinates
    /*
	Vector2f min = localBox.getTopLeft().mult(internal->fbScale).plus(internal->fbOffsetF).floor();
	Vector2f max = localBox.getBottomRight().mult(internal->fbScale).plus(internal->fbOffsetF).ceil();
	internal->fbBox = Vector4i::fromMinMax(min, max);
    */
	// DEBUG("%g %g %g %g", RECT_ARGS(internal->fbBox));

	float pixelRatio = 1.0f;// std::fmax(1.f, std::floor(APP->window->pixelRatio));
	Vector2i newFbSize = m_size;

	// Create framebuffer if a new size is needed
	if (!internal->fb || !(newFbSize == internal->fbSize)) {
		// Delete old framebuffer
		deleteFramebuffer();

		// Create a framebuffer
		//if (newFbSize.isFinite() && !newFbSize.isZero()) {
		if (newFbSize[0] > 0 && newFbSize[1] > 0) {
			// DEBUG("Creating framebuffer of size (%f, %f)", VEC_ARGS(newFbSize));
			internal->fb = nvgluCreateFramebuffer(vg, newFbSize.x(), newFbSize.y(), 0);
			FramebufferWidget_totalPixels += (newFbSize.x() * newFbSize.y());
		}

		// DEBUG("Framebuffer total pixels: %.1f Mpx", FramebufferWidget_totalPixels / 1e6);
		internal->fbSize = newFbSize;
	}
	if (!internal->fb) {
		printf("Framebuffer of size (%f, %f) could not be created for FramebufferWidget %p.", VEC_ARGS(internal->fbSize), this);
		return;
	}

	// DEBUG("Drawing to framebuffer of size (%f, %f)", VEC_ARGS(internal->fbSize));

	// Render to framebuffer
	//if (oversample == 1.0) {
		// If not oversampling, render directly to framebuffer.
		nvgluBindFramebuffer(internal->fb);
		drawFramebuffer(vg);
		nvgluBindFramebuffer(NULL);
	//}
    /*
	else {
		NVGLUframebuffer* fb = internal->fb;
		// If oversampling, create another framebuffer and copy it to actual size.
		Vector2f oversampledFbSize = internal->fbSize.mult(oversample).ceil();
		// DEBUG("Creating %0.fx oversampled framebuffer of size (%f, %f)", oversample, VEC_ARGS(internal->fbSize));
		NVGLUframebuffer* oversampledFb = nvgluCreateFramebuffer(fbVg, oversampledFbSize.x, oversampledFbSize.y, 0);

		if (!oversampledFb) {
			WARN("Oversampled framebuffer of size (%f, %f) could not be created for FramebufferWidget %p.", VEC_ARGS(oversampledFbSize), this);
			return;
		}

		// Render to oversampled framebuffer.
		nvgluBindFramebuffer(oversampledFb);
		internal->fb = oversampledFb;
		drawFramebuffer(ctx);
		internal->fb = fb;
		nvgluBindFramebuffer(NULL);

		// Use NanoVG for copying oversampled framebuffer to normal framebuffer
		nvgluBindFramebuffer(internal->fb);
		nvgBeginFrame(fbVg, internal->fbBox.size.x, internal->fbBox.size.y, 1.0);

		// Draw oversampled framebuffer
		nvgBeginPath(fbVg);
		nvgRect(fbVg, 0.0, 0.0, internal->fbSize.x, internal->fbSize.y);
		NVGpaint paint = nvgImagePattern(fbVg, 0.0, 0.0,
			internal->fbSize.x, internal->fbSize.y,
			0.0, oversampledFb->image, 1.0);
		nvgFillPaint(fbVg, paint);
		nvgFill(fbVg);

		glViewport(0.0, 0.0, internal->fbSize.x, internal->fbSize.y);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		nvgEndFrame(fbVg);
		nvgReset(fbVg);

		nvgluBindFramebuffer(NULL);
		nvgluDeleteFramebuffer(oversampledFb);
	}
    */
};


void FramebufferWidget::drawFramebuffer(NVGcontext *vg) {
	nvgSave(vg);

	float pixelRatio = internal->fbSize.x() * oversample / internal->fbBox.z();
	nvgBeginFrame(vg, internal->fbBox.w(), internal->fbBox.y(), pixelRatio);

	// Use local scaling
	nvgTranslate(vg, -internal->fbBox.x(), -internal->fbBox.y());
	nvgTranslate(vg, internal->fbOffsetF.x(), internal->fbOffsetF.y());
	nvgScale(vg, internal->fbScale.x(), internal->fbScale.y());

	// Draw children
	Widget::draw(vg);

	glViewport(0.0, 0.0, internal->fbSize.x() * oversample, internal->fbSize.y() * oversample);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	// glClearColor(0.0, 1.0, 1.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	nvgEndFrame(vg);

	// Clean up the NanoVG state so that calls to nvgTextBounds() etc during step() don't use a dirty state.
	nvgReset(vg);
	nvgRestore(vg);
}

#if 0
void FramebufferWidget::onDirty(const DirtyEvent& e) {
	setDirty();
	Widget::onDirty(e);
}


void FramebufferWidget::onContextCreate(const ContextCreateEvent& e) {
	setDirty();
	Widget::onContextCreate(e);
}


void FramebufferWidget::onContextDestroy(const ContextDestroyEvent& e) {
	deleteFramebuffer();
	setDirty();
	Widget::onContextDestroy(e);
}
#endif


NAMESPACE_END(nanogui)

