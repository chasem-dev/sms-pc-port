#ifndef SMS_MP_PUPPET_HPP
#define SMS_MP_PUPPET_HPP

// Another player, drawn in this game (ext/multiplayer): Mario's own model,
// cap, hands, FLUDD, Yoshi and the Blooper they surf on, posed each frame as that player's game posed
// them (their animation, frame and root matrix, mpg_pose). It lives in the
// stage's player group beside Mario, so the scene performs and lights it as
// it does him; it has no collision and takes nothing.
//
// Every character is Mario's model with his texture recoloured (each puppet
// loads its own copy of the files, so the local Mario keeps his colours).
// With the launcher's paint data, each player wears their own colours
// (Customize Mario's, else their character's) in place of those textures
// (mpg_dress), painted again whenever anyone's change; FLUDD too, from its own
// copy of FLUDD's files.

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
// Counts the stages set up: what was loaded for one stage lives in its
// memory, and is gone in the next (a director's address can come back).
unsigned MpStageNumber();
// Another player's FLUDD water, emitted in this game (mp_game.cpp).
void MpEmitWater(const MpgPose& pose, f32* carry);

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
	const MpgPose& pose() const { return mPose; }
	// Where it is (for the name over its head), valid while active().
	const f32* headPos() const { return mHead; }
	// Where the name over its head goes on the screen as the last frame drew
	// it (0..1 across the picture from its top left), and how strongly it
	// shows (0..1, fading out with distance from `viewer`); false when it is
	// too far away or out of view.
	bool nameSpot(const f32* viewer, f32* x, f32* y, f32* alpha) const;

private:
	void applyPose(JDrama::TGraphics* graphics);
	void blendPoses(const MpgPose& a, const MpgPose& b, f32 t, MpgPose* out);
	void showHands(int hand);
	bool buildBody();
	bool buildFludd();
	bool buildYoshi();
	bool buildSurf();
	void dress();

	int mSlot;
	int mCharacter;
	bool mSelf;
	bool mActive;
	bool mHaveSmoothed;
	MpgPose mPose;
	f32 mSmoothed[3];
	f32 mHead[3];
	u32 mLastSession;
	f32 mWaterCarry;
	int mSpawnFrames;   // since this player appeared (it grows in over the first few)
	int mGoneFrames;    // since it was last seen

	M3UModelMario* mModel;
	J3DModelData* mBodyData;
	J3DModel* mHands[2][2];  // [ma_hnd2, ma_hnd3][right, left]
	J3DModel* mCap[2];       // ma_cap1 (cap), ma_cap3 (hair, capless)
	MActor* mFludd;
	J3DModelData* mFluddData; // this puppet's own copy of FLUDD's, or null when it shares Mario's
	MActor* mNozzles[6];
	u8 mNozzleJoint;
	MActor* mYoshi;
	MActor* mSurf;           // the Blooper they ride (Ricco Harbor's, Gelato's)
	MActor* mRide;           // the boat they ride in their game, this frame (MpMirrorRide), or null
	s16 mSurfColor[4];       // its colour (GXColorS10, as the game's three)
	u16 mJointChest, mJointHandR, mJointHandL, mJointHead, mJointMHead;
	J3DDrawBuffer* mOpa;
	J3DDrawBuffer* mXlu;
	J3DDrawBuffer* mSaved[2];
	bool mSwapped;
	u32 mDressedVersion;     // mpg_looks_version when last dressed
	f32 mView[3][4];         // the camera as last drawn, for the name
	f32 mProjection[7];      // GXGetProjectionv's, as it was drawn
	bool mDrawn;             // drawn at least once since it appeared
};

// Debug: how smoothly a player moves as drawn here (their own game's player too,
// to hold the others' against).
void MpMeasureSmoothness(int slot, const MpgPose& pose);

#endif
