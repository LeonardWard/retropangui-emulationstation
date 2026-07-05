#pragma once
#ifndef ES_CORE_COMPONENTS_ANIMATED_IMAGE_COMPONENT_H
#define ES_CORE_COMPONENTS_ANIMATED_IMAGE_COMPONENT_H

#include "GuiComponent.h"

class ImageComponent;

struct AnimationFrame
{
	const char* path;
	int time;
};

struct AnimationDef
{
	AnimationFrame* frames;
	size_t frameCount;
	bool loop;
};

class AnimatedImageComponent : public GuiComponent
{
public:
	AnimatedImageComponent(Window* window);

	void load(const AnimationDef* def); // no reference to def is kept after loading is complete

	void reset(); // set to frame 0

	// 애니메이션을 그 자리에서 멈추거나(현재 프레임 유지, 화면에서 안 사라짐)
	// 다시 재생. load()는 내부적으로 mEnabled=true로 시작하므로 기본은 재생 중.
	void setAnimating(bool animating) { mEnabled = animating; }

	void update(int deltaTime) override;
	void render(const Transform4x4f& trans) override;

	void onSizeChanged() override;

private:
	typedef std::pair<std::unique_ptr<ImageComponent>, int> ImageFrame;

	std::vector<ImageFrame> mFrames;

	bool mLoop;
	bool mEnabled;
	int mFrameAccumulator;
	int mCurrentFrame;
};

#endif // ES_CORE_COMPONENTS_ANIMATED_IMAGE_COMPONENT_H
