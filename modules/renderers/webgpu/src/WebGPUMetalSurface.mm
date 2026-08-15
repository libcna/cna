// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUMetalSurface.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <stdexcept>

@interface CNAWebGPUSurfaceView : NSView
- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale;
@end

@implementation CNAWebGPUSurfaceView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        self.layer.opaque = YES;
    }
    return self;
}

- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale
{
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    layer.contentsScale = displayScale > 0.0f ? displayScale : 1.0f;
    layer.drawableSize = CGSizeMake(std::max(1, width), std::max(1, height));
}

- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
@end

namespace CNA::Internal::Renderers::WebGPU
{
    void* CreateWebGPUMetalLayer(const CNA::Platform::NativeWindowHandle& handle,
                                 int width, int height, float displayScale, void*& owner)
    {
        CNA::Platform::CocoaNativeWindow native;
        if (!CNA::Platform::TryGetCocoa(handle, native))
            throw std::runtime_error("CNA WebGPU: a Cocoa native window is required");
        NSWindow* window = (NSWindow*)native.window;
        NSView* contentView = [window contentView];
        if (contentView == nil)
            throw std::runtime_error("CNA WebGPU: Cocoa window has no content view");

        CNAWebGPUSurfaceView* view = [[CNAWebGPUSurfaceView alloc] initWithFrame:[contentView bounds]];
        if (view == nil)
            throw std::runtime_error("CNA WebGPU: failed to create a Metal surface view");
        [contentView addSubview:view];
        [view updateDrawableWidth:width height:height displayScale:displayScale];
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;
        if (layer == nil)
        {
            [view removeFromSuperview];
            [view release];
            throw std::runtime_error("CNA WebGPU: layer-backed view did not create a CAMetalLayer");
        }
        owner = view;
        return layer;
    }

    void ResizeWebGPUMetalLayer(void* owner, int width, int height, float displayScale)
    {
        [(CNAWebGPUSurfaceView*)owner updateDrawableWidth:width height:height
                                            displayScale:displayScale];
    }

    void DestroyWebGPUMetalLayer(void*& owner)
    {
        CNAWebGPUSurfaceView* view = (CNAWebGPUSurfaceView*)owner;
        if (view != nil)
        {
            [view removeFromSuperview];
            [view release];
            owner = nullptr;
        }
    }
}
