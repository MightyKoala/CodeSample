#pragma once
#include <Engine\ComponentSystem\Component\Component.h>
#include <BulletPhysics\BulletDynamics\Character\btKinematicCharacterController.h>

class btCollisionWorld;

class CharacterPhysicsComponent : public Component, public CU::Subscriber<SJsonFileChangedMessage>
{
public:
	CharacterPhysicsComponent(const std::string& aFilePath);
	~CharacterPhysicsComponent();

	virtual void Init() override;
	virtual void Update() override;
	virtual void OnMessage(ComponentMessage* aMessage) override;
	virtual CU::NotifyResponse Notify(const SJsonFileChangedMessage& aMessage) override;
	void ResetCharacterPhysics();
	void ResetCollisionFlags();

	const Vector3f& GetWantedDirection() const;
	const bool GetIsJumping() const;
	const ControlSettings& GetSettings() const;
	btKinematicCharacterController* GetCharacterPhysicsPointer();
private:
	void SyncMovementData(const float aDeltaTime, const bool aCanjump);
	void Jump();
	void UpdateMovement(const float aDeltaTime);
	void ChangePosition(const Vector3f& aPosition);
	void SetPhysicsWorld();
	void SetSettings(const std::string& aFilePath = "");
	ControlSettings mySettings;

	Vector3f myPosition;
	float mySpeed;
	Vector3f myWantedDirection;
	float myRotationSpeed;
	Vector3f myMoveDirection;
	float myActionSyncTimer;
	float myFallTimer;
	const float TimeFallingToDeath;
	static const float SyncFrameRate;
	int myDefaultCollisionFlags;
	bool myShouldJump;
	btKinematicCharacterController* myCharacterPhysics;
	btCollisionWorld* myPhysicsWorld;
	std::string myFilePath;
};