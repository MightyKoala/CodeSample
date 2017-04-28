#include "stdafx.h"
#include "CharacterPhysicsComponent.h"
#include <BulletCollision\CollisionDispatch\btGhostObject.h>
#include <Components\Player\PlayerInputComponent.h>
#include <Components\Health\HealthComponent.h>
#include <Level\Level.h>
#include <Engine\Level\LevelManager.h>
#include <Engine\Json\WrappedJson.h>
#include <Engine\Utility\FileHelper.h>
#include <CommonUtilities\Network\MessageHandling\MessageTypes.h>
#include <CommonUtilities\Network\MessageHandling\MessageAssembler.h>
#include <CommonUtilities\Network\MessageHandling\GameObjectMessages.h>

const float CharacterPhysicsComponent::SyncFrameRate = 100.0f;

CharacterPhysicsComponent::CharacterPhysicsComponent(const std::string & aFilePath)
	: TimeFallingToDeath(3.0f)
{
	myParent = nullptr;
	myPhysicsWorld = nullptr;
	myCharacterPhysics = nullptr;

	myPosition = Vector3f::Zero;
	myWantedDirection = Vector3f::Zero;
	myShouldJump = false;
	myFilePath = aFilePath;
	myActionSyncTimer = 0.01f;
	myFallTimer = 0.0f;

	CU::Subscriber<SJsonFileChangedMessage>::Subscribe();
}

CharacterPhysicsComponent::~CharacterPhysicsComponent()
{
	if (myPhysicsWorld != nullptr && myCharacterPhysics != nullptr)
	{
		myPhysicsWorld->removeCollisionObject(myCharacterPhysics->getGhostObject());
		delete myCharacterPhysics;
		myCharacterPhysics = nullptr;
	}
}

void CharacterPhysicsComponent::Init()
{
	SetSettings(myFilePath);
}

void CharacterPhysicsComponent::Update()
{
	const float deltaTime = MainSingleton::GetGameTime()->GetDeltaTime().GetSeconds();
	const bool canJump = myCharacterPhysics->canJump();

	if (canJump == false)
	{
		HealthComponent *healthComp = myParent->GetComponent<HealthComponent>();
		if (healthComp)
		{
			if (healthComp->GetHealth() > 0)
			{
				myFallTimer += deltaTime;
				if (myFallTimer >= TimeFallingToDeath)
				{
					myFallTimer = 0.0f;
					healthComp->SetHealth(0);
					return;
				}
			}
		}
	}
	else
	{
		myFallTimer = 0.0f;
	}

	SyncMovementData(deltaTime, canJump);

	if (myPhysicsWorld == nullptr)
	{
		SetPhysicsWorld();
	}

	UpdateMovement(deltaTime);
}

void CharacterPhysicsComponent::OnMessage(ComponentMessage* aMessage)
{
	switch (aMessage->GetId())
	{
	case eComponentMessageId::eMoveObject:
	{
		MovePhysicsObjectMessage* moveMessage = dynamic_cast<MovePhysicsObjectMessage*>(aMessage);
		if (moveMessage != nullptr)
		{
			//std::cout << "Moved" << std::endl;
			myMoveDirection = moveMessage->myDirection;
			myWantedDirection = moveMessage->myDirection;
			myShouldJump = moveMessage->myShouldJump;
		}
		break;
	}
	case eComponentMessageId::eSyncMessage:
	{
		SyncObjectPosition* syncMessage = dynamic_cast<SyncObjectPosition*>(aMessage);
		if (syncMessage != nullptr)
		{
			if (myParent != nullptr)
			{
				//float difference = (syncMessage->myPosition - myParent->GetTransform().GetWorldPosition()).Length();
				//if (difference > 1.0f)
				{
					myPosition = syncMessage->myPosition;
					btVector3 pos = btVector3(myPosition.x, myPosition.y, myPosition.z);
					if (myCharacterPhysics != nullptr)
					{
						myCharacterPhysics->getGhostObject()->getWorldTransform().setOrigin(pos);
					}
				}
			}
		}
		break;
	}
	case eComponentMessageId::eDied:
	{
		if (myParent != MainSingleton::GetGameObjectPool()->GetGameObjectByName("Player").Get())
		{
			DL_PRINT_CONSOLE(std::string("Removing enemy with ID: " + std::to_string(myParent->GetID())).c_str(), CONSOLE_TEXT_COLOR_RED);
			//dynamic_cast<Level*>(MainSingleton::GetLevelManager()->GetCurrentLevel().get())->GetPhysicsWorld().RemoveGameObject(myParent);
			myParent->DeleteComponent<CharacterPhysicsComponent>();
			return;
		}
		break;
	}
	}
}

void CharacterPhysicsComponent::SyncMovementData(const float aDeltaTime, const bool aCanjump)
{
	CharacterInfoMessage infoMessage;
	infoMessage.myDirection = myWantedDirection;
	infoMessage.myIsJumping = !aCanjump;
	BroadcastMessage(&infoMessage);

	myActionSyncTimer -= aDeltaTime;
	if (myActionSyncTimer <= 0.0f)
	{
		myActionSyncTimer = 1.0f / SyncFrameRate;
		Net::CharacterInformationMessage netInfoMessage = Net::MessageAssembler::GetInstance()->CreateMessage<Net::CharacterInformationMessage>();
		netInfoMessage.myObjectID = myParent->GetID();
		netInfoMessage.myWantedDirection = myWantedDirection;
		netInfoMessage.myIsJumping = !aCanjump;
		MainSingleton::GetNetworkManager()->SendMessageToServer(netInfoMessage);
	}
}

void CharacterPhysicsComponent::Jump()
{
	myCharacterPhysics->jump();
	myShouldJump = false;
}

void CharacterPhysicsComponent::UpdateMovement(const float aDeltaTime)
{
	Vector3f newPosition;

	myCharacterPhysics->setWalkDirection(btVector3(myWantedDirection.x, myWantedDirection.y, myWantedDirection.z) * mySpeed);
	myWantedDirection = Vector3f::Zero;

	if (myShouldJump == true)
	{
		Jump();
	}

	if (myCharacterPhysics->getGhostObject()->getBroadphaseHandle() == nullptr)
	{
		return;
	}
	if (myCharacterPhysics->getGhostObject()->getCollisionFlags() != btCollisionObject::CF_NO_CONTACT_RESPONSE)
	{
		myCharacterPhysics->updateAction(myPhysicsWorld, aDeltaTime);
	}
	btVector3 bulletPosition = myCharacterPhysics->GetCurrentPosition();
	newPosition = Vector3f(bulletPosition.x(), bulletPosition.y(), bulletPosition.z());

	if (myPosition != newPosition)
	{
		ChangePosition(newPosition);
	}
}

void CharacterPhysicsComponent::ChangePosition(const Vector3f& aPosition)
{
	myPosition = aPosition;

	MoveObjectMessage moveMessage;
	moveMessage.myPosition = myPosition;
	moveMessage.myDirection = myMoveDirection;

	BroadcastMessage(&moveMessage);
}

void CharacterPhysicsComponent::SetPhysicsWorld()
{
	Level* currentLevel = dynamic_cast<Level*>(MainSingleton::GetLevelManager()->GetCurrentLevel().get());
	if (currentLevel == nullptr)
	{
		return;
	}
	myPhysicsWorld = currentLevel->GetPhysicsWorld().GetPhysicsWorld();

	if (myPhysicsWorld == nullptr)
	{
		return;
	}
}

CU::NotifyResponse CharacterPhysicsComponent::Notify(const SJsonFileChangedMessage &aMessage)
{
	if (aMessage.myFileName == myFilePath)
	{
		SetSettings(myFilePath);
		return CU::NotifyResponse::Stop;
	}
	return CU::NotifyResponse::Continue;
}

const Vector3f& CharacterPhysicsComponent::GetWantedDirection() const
{
	return myWantedDirection;
}

const bool CharacterPhysicsComponent::GetIsJumping() const
{
	return myShouldJump;
}

void CharacterPhysicsComponent::SetSettings(const std::string &aFilePath)
{
	if (aFilePath != "")
	{
		myFilePath = aFilePath;
	}

	if (myPhysicsWorld == nullptr)
	{
		Level* currentLevel = dynamic_cast<Level*>(MainSingleton::GetLevelManager()->GetCurrentLevel().get());
		if (currentLevel == nullptr)
		{
			return;
		}
		myPhysicsWorld = currentLevel->GetPhysicsWorld().GetPhysicsWorld();

		if (myPhysicsWorld == nullptr)
		{
			return;
		}
	}

	if (!FileHelper::FileExists(myFilePath))
	{
		DL_PRINT_CONSOLE("Player stats file not found!", CONSOLE_TEXT_COLOR_RED);
		return;
	}

	rapidjson::Document* document = WrappedJson::OpenNew(myFilePath);
	mySettings.myHeight = (*document)["PlayerCollisionHeight"].GetFloat();
	mySettings.myRadius = (*document)["PlayerCollisionRadius"].GetFloat();
	mySettings.myStepHeight = (*document)["PlayerStepHeight"].GetFloat();
	mySettings.myGravityMultiplier = (*document)["PlayerGravityMultiplier"].GetFloat();
	mySettings.mySpeed = (*document)["PlayerSpeed"].GetFloat();
	mySettings.mySensitivity = 0.0001f * (*document)["PlayerSensitivity"].GetFloat();
	mySettings.myFallSpeed = (*document)["PlayerFallSpeed"].GetFloat();
	mySettings.myJumpSpeed = (*document)["PlayerJumpSpeed"].GetFloat();

	if (myParent->GetTransform().GetWorldPosition() == Vector3f::Zero)
	{
		myParent->GetTransform().SetWorldPosition(Vector3f(0.0f, (*document)["PlayerHeight"].GetFloat(), 0.0f));
	}

	mySpeed = mySettings.mySpeed;
	myRotationSpeed = mySettings.mySensitivity;

	printf("Stats");
	if (myParent != nullptr)
	{
		printf("GetComp");
		if (myParent->GetComponent<PlayerInputComponent>() != nullptr)
		{
			printf("SetSens");
			myParent->GetComponent<PlayerInputComponent>()->SetSensitivity(myRotationSpeed);
		}
	}

	btCapsuleShape *playerShape = new btCapsuleShape(mySettings.myRadius, mySettings.myHeight);
	btPairCachingGhostObject *ghostObject = new btPairCachingGhostObject();
	ghostObject->setUserPointer(myParent);
	Vector3f position = myParent->GetTransform().GetWorldPosition();
	ghostObject->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(position.x, position.y, position.z)));
	ghostObject->setCollisionShape(playerShape);
	ghostObject->setCollisionFlags(ghostObject->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
	myCharacterPhysics = new btKinematicCharacterController(ghostObject, playerShape, mySettings);
	myPhysicsWorld->addCollisionObject(ghostObject, btBroadphaseProxy::CharacterFilter, btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
	myDefaultCollisionFlags = ghostObject->getCollisionFlags();
	dynamic_cast<Level*>(MainSingleton::GetLevelManager()->GetCurrentLevel().get())->GetPhysicsWorld().AddGameObject(myParent, ghostObject);
}

void CharacterPhysicsComponent::ResetCharacterPhysics()
{
	myPhysicsWorld = nullptr;
	SetSettings();
}

const ControlSettings& CharacterPhysicsComponent::GetSettings() const
{
	return mySettings;
}

btKinematicCharacterController * CharacterPhysicsComponent::GetCharacterPhysicsPointer()
{
	return myCharacterPhysics;
}

void CharacterPhysicsComponent::ResetCollisionFlags()
{
	GetCharacterPhysicsPointer()->getGhostObject()->setCollisionFlags(myDefaultCollisionFlags);
}
