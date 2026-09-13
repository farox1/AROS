/*
 * Minimal drm_vblank.h for the AROS nouveau port.
 *
 * On AROS all DRM vblank machinery is compiled out (#if !defined(__AROS__)
 * in disp.c / nouveau_display.c), because the compositor updates the
 * framebuffer directly and never waits for vblank interrupts. This header
 * exists only so that #include <drm/drm_vblank.h> resolves; it declares the
 * types and functions for reference, matching the upstream interface.
 */

#ifndef _DRM_VBLANK_H_
#define _DRM_VBLANK_H_

#include <linux/types.h>

struct drm_device;
struct drm_crtc;
struct drm_pending_vblank_event;
struct drm_vblank_crtc;
struct drm_file;

void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
				struct drm_pending_vblank_event *e);
bool drm_crtc_handle_vblank(struct drm_crtc *crtc);
int drm_crtc_vblank_get(struct drm_crtc *crtc);
void drm_crtc_vblank_put(struct drm_crtc *crtc);
void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe);
void drm_crtc_vblank_off(struct drm_crtc *crtc);
void drm_crtc_vblank_on(struct drm_crtc *crtc);
u64 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc);
int drm_crtc_vblank_count(struct drm_crtc *crtc);

#endif /* _DRM_VBLANK_H_ */
