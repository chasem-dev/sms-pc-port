#ifndef SMS_MP_PUPPET_HPP
#define SMS_MP_PUPPET_HPP

// Another player, drawn in this game (ext/multiplayer): Mario's own model,
// cap, hands, FLUDD and Yoshi, posed each frame as that player's game posed
// them (their animation, frame and root matrix, mpg_pose). It lives in the
// stage's player group beside Mario, so the scene performs and lights it as
// it does him; it has no collision and takes nothing.
//
// Mario, Luigi and Metal Mario are Mario's model with his texture recoloured
// (each puppet loads its own copy of the files, so the local Mario keeps his
// colours); Peach is Peach's own model when the disc has her (mp_peach.cpp),
// posed from Mario's animation.

#include "../mp_game_api.h"
#include <Strategic/HitActor.hpp>

class J3DModel;
class J3DModelData;
class J3DDrawBuffer;
class M3UModelMario;
class MActor;

class TMario;
// Mario's look this frame, as a pose (mp_game.cpp).
void MpCapturePose(TMario* mario, MpgPose* pose);

class TMpPuppet : public THitActor {
public:
	// `self`: this game's own player in their character, posed from Mario
	// here each frame (the game then leaves out Mario's own models).
	TMpPuppet(int slot, bool self);

	// Builds the models; false when there was not room for them.
	bool init();
	virtual void perform(u32 cue, JDrama::TGraphics* graphics);

	int slot() const { return mSlot; }
	bool active() const { return mActive; }
	// Where it is (for the name over its head), valid while active().
	const f32* headPos() const { return mHead; }

private:
	void applyPose(JDrama::TGraphics* graphics);
	void showHands(int hand);
	bool buildBody();
	bool buildFludd();
	bool buildYoshi();
	bool buildPeach();
	void posePeach(f32 (*base)[4]);

	int mSlot;
	int mCharacter;
	bool mSelf;
	bool mActive;
	bool mHaveSmoothed;
	MpgPose mPose;
	f32 mSmoothed[3];
	f32 mHead[3];
	u32 mLastSession;

	M3UModelMario* mModel;   // Mario-shaped body (for Peach, the skeleton she copies)
	J3DModelData* mBodyData;
	J3DModel* mHands[2][2];  // [ma_hnd2, ma_hnd3][right, left]
	J3DModel* mCap[2];       // ma_cap1 (cap), ma_cap3 (hair, capless)
	MActor* mFludd;
	MActor* mNozzles[6];
	u8 mNozzleJoint;
	MActor* mYoshi;
	J3DModel* mPeach;        // Peach's body, when this puppet is her
	J3DModel* mPeachParts[3]; // her hair, left hand, right hand
	struct PeachRig* mRig;   // how her joints follow Mario's (mp_puppet.cpp)
	u16 mJointChest, mJointHandR, mJointHandL, mJointHead, mJointMHead;
	J3DDrawBuffer* mOpa;
	J3DDrawBuffer* mXlu;
	J3DDrawBuffer* mSaved[2];
	bool mSwapped;
};

#endif
