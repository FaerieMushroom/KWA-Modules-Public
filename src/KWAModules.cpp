#include "KWAModules.hpp"

Plugin *pluginInstance;


void init(Plugin *p) {
	pluginInstance = p;
	p->addModel(modelSixtyFourGatePitchSeq);
	p->addModel(modelSixtyFourGatePitchSeqKnobs);
	p->addModel(modelEightGateSequencer);
	p->addModel(modelEightGateSequencerChild);
	p->addModel(modelSequencerController);
	p->addModel(modelSequencerEnd);
	p->addModel(modelSixtyFourGatePitchSeqExpander);
}
