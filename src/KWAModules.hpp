#include <rack.hpp>

using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

// Declare each Model, defined in each module source file
extern Model *modelSixtyFourGatePitchSeq;
extern Model *modelSixtyFourGatePitchSeqKnobs;
extern Model *modelEightGateSequencer;
extern Model *modelEightGateSequencerChild;
extern Model *modelSequencerController;
extern Model *modelSequencerEnd;
extern Model *modelSixtyFourGatePitchSeqExpander;

//component templates

struct MidSizeButton : app::SvgSwitch {
	MidSizeButton() {
		momentary = false;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/n_RubberButton.svg")));
	}
};

struct MomentaryMidSizeButton : app::SvgSwitch {
	MomentaryMidSizeButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/n_RubberButton.svg")));
	}
};

struct RubberRectButtonLarge : app::SvgSwitch {
	RubberRectButtonLarge() {
		momentary = true;
		shadow->opacity = 0.0;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/n_RubberRectButton.svg")));
	}
};

template <typename TBase>
struct RoundRectLight : TBase {
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		float r = std::min( this->box.size.x, this->box.size.y) * 0.11;
		nvgRoundedRect(args.vg, 0.0, 0.0, this->box.size.x, this->box.size.y, r);
		if (this->bgColor.a > 0.0) {
			nvgFillColor(args.vg, this->bgColor);
			nvgFill(args.vg);
		}
		if (this->color.a > 0.0) {
			nvgFillColor(args.vg, this->color);
			nvgFill(args.vg);
		}
		if (this->borderColor.a > 0.0) {
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, this->borderColor);
			nvgStroke(args.vg);
		}
	}
};


template <typename TBase>
struct LargerButtonCenterLight : TBase {
	LargerButtonCenterLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(math::Vec(7.f, 7.f));
	}
};
template <typename TBase>
struct LargerButtonLight : TBase {
	LargerButtonLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(math::Vec(11.3f, 11.3f));
	}
};

template <typename TBase>
struct MidsizeButtonCenterLight : TBase {
	MidsizeButtonCenterLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(math::Vec(4, 4));
	}
};
template <typename TBase>
struct MidsizeButtonLight : TBase {
	MidsizeButtonLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(math::Vec(8, 8));
	}
};
