#ifndef SMS_MP_PEACH_HPP
#define SMS_MP_PEACH_HPP

// Peach's model, read from the disc (mp_peach.cpp), once per stage.

class J3DModelData;

struct MpPeachParts {
	J3DModelData* body;
	J3DModelData* hair; // her ponytail, on her neck joint
	J3DModelData* handL;
	J3DModelData* handR;
};

// Her parts in this stage's memory; null when the disc lacks them.
const MpPeachParts* MpPeachLoad();

#endif
