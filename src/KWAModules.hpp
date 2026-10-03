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

/** The step at position p of a vertical playhead order, for a step count of n.
Only the first n steps are ever active, so this reorders 0..n-1 rather than slicing the full
64 step order. That matters: at n of 8 or less every active step sits in the top row, so the
walk comes out the same as a plain Ascend or Descend. At n of 9 it becomes 0, 8, 1, 2 and so
on, still only touching steps the Steps knob has switched on.
The 8x8 grid is row major, so a step index is row * 8 + column and a column walk visits a
column top to bottom before moving right. Mode 4 is top to bottom then left to right, mode 5
is bottom to top then right to left. */
inline int verticalStepAt(int p, int mode, int n) {
	if (n < 1) n = 1;
	if (p < 0) p = 0;
	if (p >= n) p = n - 1;
	const int startCol = (mode == 4) ? 0 : 7;
	const int colStep = (mode == 4) ? 1 : -1;
	for (int c = 0; c < 8; c++) {
		const int col = startCol + colStep * c;
		// This column's active rows run 0 to count-1, since only the first n steps play
		int count = 0;
		for (int r = 0; r < 8; r++)
			if (col + r * 8 < n) count++;
		if (p < count) {
			const int row = (mode == 4) ? p : (count - 1 - p);
			return col + row * 8;
		}
		p -= count;
	}
	return 0;
}

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
