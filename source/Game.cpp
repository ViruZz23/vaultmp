#include "Game.h"

unsigned char Game::game = 0x00;
RakNetGUID Game::server;

#ifdef VAULTMP_DEBUG
Debug* Game::debug = NULL;
#endif

using namespace Values;

#ifdef VAULTMP_DEBUG
void Game::SetDebugHandler(Debug* debug)
{
	Game::debug = debug;

	if (debug)
		debug->Print("Attached debug handler to Game class", true);
}
#endif

void Game::AdjustZAngle(double& Z, double diff)
{
	Z += diff;

	if (Z > 360.0)
		Z -= 360.0;

	else if (Z < 0.00)
		Z += 360.0;
}

NetworkResponse Game::Authenticate(string password)
{
	FactoryObject reference = GameFactory::GetObject(PLAYER_REFERENCE);
	Player* self = vaultcast<Player>(reference);

	return NetworkResponse{Network::CreateResponse(
		PacketFactory::CreatePacket(ID_GAME_AUTH, self->GetName().c_str(), password.c_str()),
		HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
	};
}

void Game::Startup()
{
	FactoryObject reference = GameFactory::GetObject(PLAYER_REFERENCE);
	Player* self = vaultcast<Player>(reference);

	Interface::StartSetup();

	Interface::SetupCommand("GetPos", ParamContainer{self->GetReferenceParam(), Object::Param_Axis()});
	Interface::SetupCommand("GetPos", ParamContainer{Player::CreateFunctor(FLAG_ENABLED | FLAG_NOTSELF | FLAG_ALIVE), Object::Param_Axis()}, 30);
	Interface::SetupCommand("GetAngle", ParamContainer{self->GetReferenceParam(), RawParameter(vector<string> {API::RetrieveAxis_Reverse(Axis_X), API::RetrieveAxis_Reverse(Axis_Z)})});
	Interface::SetupCommand("GetActorState", ParamContainer{self->GetReferenceParam(), Player::CreateFunctor(FLAG_MOVCONTROLS, self->GetNetworkID())});
	Interface::SetupCommand("GetParentCell", ParamContainer{Player::CreateFunctor(FLAG_ALIVE)}, 30);
	Interface::SetupCommand("ScanContainer", ParamContainer{self->GetReferenceParam()}, 50);
	Interface::SetupCommand("GetDead", ParamContainer{Player::CreateFunctor(FLAG_ENABLED | FLAG_ALIVE)}, 30);
	Interface::SetupCommand("GetActorValueHealth", ParamContainer{self->GetReferenceParam(), RawParameter(vector<string>{
		API::RetrieveValue_Reverse(Fallout::ActorVal_Health),
		API::RetrieveValue_Reverse(Fallout::ActorVal_Head),
		API::RetrieveValue_Reverse(Fallout::ActorVal_Torso),
		API::RetrieveValue_Reverse(Fallout::ActorVal_LeftArm),
		API::RetrieveValue_Reverse(Fallout::ActorVal_RightArm),
		API::RetrieveValue_Reverse(Fallout::ActorVal_LeftLeg),
		API::RetrieveValue_Reverse(Fallout::ActorVal_RightLeg)})}, 30);

	// we could exclude health values here
	Interface::SetupCommand("GetActorValue", ParamContainer{self->GetReferenceParam(), Actor::Param_ActorValues()}, 100);
	Interface::SetupCommand("GetBaseActorValue", ParamContainer{self->GetReferenceParam(), Actor::Param_ActorValues()}, 200);

	Interface::EndSetup();
}

template <typename T>
void Game::FutureSet(weak_ptr<Lockable> data, T t)
{
	shared_ptr<Lockable> shared = data.lock();
	Lockable* locked = shared.get();

	if (locked == NULL)
		throw VaultException("Storage has expired");

	Value<T>* store = dynamic_cast<Value<T>*>(locked);

	if (store == NULL)
		throw VaultException("Storage is corrupted");

	store->set(t);
	store->set_promise();
}
template void Game::FutureSet(weak_ptr<Lockable> data, unsigned int t);
template void Game::FutureSet(weak_ptr<Lockable> data, bool t);

inline
void Game::AsyncTasks()
{

}

template <typename A, typename... Values>
void Game::AsyncTasks(A && async, Values && ... more)
{
	if (async.second > chrono::milliseconds(0))
		this_thread::sleep_for(async.second);

	async.first.wait();
	AsyncTasks(more...);
}

void Game::LoadGame(string savegame)
{
	static string last_savegame;

	if (savegame.empty())
		savegame = last_savegame;
	else
	{
		Utils::RemoveExtension(savegame);
		last_savegame = savegame;
	}

	shared_ptr<Value<bool>> store(new Value<bool>);
	unsigned int key = Lockable::Share(store);

	Interface::StartDynamic();

	Interface::ExecuteCommand("Load", ParamContainer{RawParameter(savegame)}, key);

	Interface::EndDynamic();

	try
	{
		store.get()->get_future(chrono::seconds(60));
	}
	catch (exception& e)
	{
		throw VaultException("Loading of savegame %s failed (%s)", savegame.c_str(), e.what());
	}

	// ready state
}

void Game::LoadEnvironment()
{
	vector<NetworkID> reference = GameFactory::GetIDObjectTypes(ALL_OBJECTS);

	for (NetworkID& id : reference)
	{
		FactoryObject reference = GameFactory::GetObject(id);

		if ((*reference)->GetReference() != PLAYER_REFERENCE)
		{
			if (!(*reference)->IsPersistent())
				(*reference)->SetReference(0x00000000);

			switch (GameFactory::GetType(*reference))
			{
				case ID_OBJECT:
					NewObject(reference);
					break;

					//case ID_ITEM: // don't create items of containers
					//NewItem(reference);
					//break;

				case ID_CONTAINER:
					NewContainer(reference);
					break;

				case ID_ACTOR:
					NewActor(reference);
					break;

				case ID_PLAYER:
					NewPlayer(reference);
					break;
			}
		}
		else
			SetName(reference);
	}

	Interface::StartDynamic();

	Interface::ExecuteCommand("GetControl", ParamContainer{RawParameter(API::RetrieveAllControls())});

	Interface::EndDynamic();
}

void Game::UIMessage(string message)
{
	Interface::StartDynamic();

	Interface::ExecuteCommand("UIMessage", ParamContainer{RawParameter(message)});

	Interface::EndDynamic();
}

void Game::NewObject(FactoryObject& reference)
{
	Object* object = vaultcast<Object>(reference);

	if (!object->GetReference())
	{
		shared_ptr<Value<unsigned int>> store(new Value<unsigned int>);
		unsigned int key = Lockable::Share(store);

		PlaceAtMe(PLAYER_REFERENCE, object->GetBase(), 1, key);

		NetworkID id = object->GetNetworkID();
		GameFactory::LeaveReference(reference);

		unsigned int refID;

		try
		{
			refID = store.get()->get_future(chrono::seconds(15));
		}
		catch (exception& e)
		{
			throw VaultException("Object creation with baseID %08X and NetworkID %lld failed (%s)", object->GetBase(), object->GetNetworkID(), e.what());
		}

		reference = GameFactory::GetObject(id);
		object = vaultcast<Object>(reference);

		object->SetReference(refID);
	}
	else
	{
		// existing objects
	}

	SetName(reference);
	SetPos(reference);
	SetAngle(reference);

	// maybe more
}

void Game::NewItem(FactoryObject& reference)
{
	NewObject(reference);

	// set condition
}

void Game::NewContainer(FactoryObject& reference)
{
	NewObject(reference);
	RemoveAllItems(reference);

	Container* container = vaultcast<Container>(reference);
	vector<FactoryObject> items = GameFactory::GetMultiple(vector<NetworkID>(container->GetItemList().begin(), container->GetItemList().end()));

	for (FactoryObject& _item : items)
	{
		AddItem(vector<FactoryObject> {reference, _item});
		Item* item = vaultcast<Item>(_item);

		//debug->PrintFormat("ID: %lld, %s, %08X, %d, %d, %d, %d", true, item->GetNetworkID(), item->GetName().c_str(), item->GetBase(), (int)item->GetItemEquipped(), (int)item->GetItemSilent(), (int)item->GetItemStick(), item->GetItemCount());

		if (item->GetItemEquipped())
			EquipItem(vector<FactoryObject> {reference, _item});
	}
}

void Game::NewActor(FactoryObject& reference)
{
	NewContainer(reference);

	SetRestrained(reference, true);

	vector<unsigned char> values = API::RetrieveAllValues();

	for (unsigned char value : values)
	{
		SetActorValue(reference, true, value);
		SetActorValue(reference, false, value);
	}

	Actor* actor = vaultcast<Actor>(reference);

	if (actor->GetActorAlerted())
		SetActorAlerted(reference).join();

	if (actor->GetActorSneaking())
		SetActorSneaking(reference).join();

	if (actor->GetActorMovingAnimation() != AnimGroup_Idle)
		SetActorMovingAnimation(reference);
}

void Game::NewPlayer(FactoryObject& reference)
{
	NewActor(reference);

	// ...
}

void Game::RemoveObject(FactoryObject reference)
{
	Object* object = vaultcast<Object>(reference);

	if (object->SetEnabled(false))
		ToggleEnabled(reference);

	Interface::StartDynamic();

	Interface::ExecuteCommand("MarkForDelete", ParamContainer{object->GetReferenceParam()});

	Interface::EndDynamic();
}

void Game::PlaceAtMe(FactoryObject reference, unsigned int baseID, unsigned int count, unsigned int key)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	PlaceAtMe(container->GetReference(), baseID, count, key);
}

void Game::PlaceAtMe(unsigned int refID, unsigned int baseID, unsigned int count, unsigned int key)
{
	Interface::StartDynamic();

	Interface::ExecuteCommand("PlaceAtMe", ParamContainer{RawParameter(refID), RawParameter(baseID), RawParameter(count)}, key);

	Interface::EndDynamic();
}

void Game::ToggleEnabled(FactoryObject reference)
{
	Object* object = vaultcast<Object>(reference);

	Interface::StartDynamic();

	if (object->GetEnabled())
		Interface::ExecuteCommand("Enable", ParamContainer{object->GetReferenceParam(), RawParameter(true)});
	else
		Interface::ExecuteCommand("Disable", ParamContainer{object->GetReferenceParam(), RawParameter(false)});

	Interface::EndDynamic();
}

void Game::Delete(FactoryObject& reference)
{
	RemoveObject(reference);
	GameFactory::DestroyInstance(reference);
}

void Game::SetName(FactoryObject reference)
{
	Object* object = vaultcast<Object>(reference);
	string name = object->GetName();

	Interface::StartDynamic();

	Interface::ExecuteCommand("SetName", ParamContainer{object->GetReferenceParam(), RawParameter(object->GetName())});

	Interface::EndDynamic();
}

void Game::SetRestrained(FactoryObject reference, bool restrained)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	//bool restrained = actor->GetActorRestrained();

	Interface::StartDynamic();

	Interface::ExecuteCommand("SetRestrained", ParamContainer{actor->GetReferenceParam(), RawParameter(restrained)});

	Interface::EndDynamic();
}

void Game::SetPos(FactoryObject reference)
{
	Object* object = vaultcast<Object>(reference);

	if (!object->HasValidCoordinates())
		return;

	Lockable* key = NULL;

	Interface::StartDynamic();

	key = object->SetGamePos(Axis_X, object->GetNetworkPos(Axis_X));

	Interface::ExecuteCommand("SetPos", ParamContainer{object->GetReferenceParam(), RawParameter(API::RetrieveAxis_Reverse(Axis_X)), RawParameter(object->GetNetworkPos(Axis_X))}, key ? key->Lock() : 0);

	key = object->SetGamePos(Axis_Y, object->GetNetworkPos(Axis_Y));

	Interface::ExecuteCommand("SetPos", ParamContainer{object->GetReferenceParam(), RawParameter(API::RetrieveAxis_Reverse(Axis_Y)), RawParameter(object->GetNetworkPos(Axis_Y))}, key ? key->Lock() : 0);

	key = object->SetGamePos(Axis_Z, object->GetNetworkPos(Axis_Z));

	Interface::ExecuteCommand("SetPos", ParamContainer{object->GetReferenceParam(), RawParameter(API::RetrieveAxis_Reverse(Axis_Z)), RawParameter(object->GetNetworkPos(Axis_Z))}, key ? key->Lock() : 0);

	Interface::EndDynamic();
}

void Game::SetAngle(FactoryObject reference)
{
	Object* object = vaultcast<Object>(reference);

	Interface::StartDynamic();

	Interface::ExecuteCommand("SetAngle", ParamContainer{object->GetReferenceParam(), RawParameter(API::RetrieveAxis_Reverse(Axis_X)), RawParameter(object->GetAngle(Axis_X))});

	double value = object->GetAngle(Axis_Z);
	Actor* actor = vaultcast<Actor>(object);

	if (actor)
	{
		if (actor->GetActorMovingXY() == 0x01)
			AdjustZAngle(value, -45.0);

		else if (actor->GetActorMovingXY() == 0x02)
			AdjustZAngle(value, 45.0);
	}

	Interface::ExecuteCommand("SetAngle", ParamContainer{object->GetReferenceParam(), RawParameter(API::RetrieveAxis_Reverse(Axis_Z)), RawParameter(value)});

	Interface::EndDynamic();
}

void Game::MoveTo(vector<FactoryObject> reference, bool cell, unsigned int key)
{
	Object* object = vaultcast<Object>(reference[0]);
	Object* object2 = vaultcast<Object>(reference[1]);

	Interface::StartDynamic();

	ParamContainer param_MoveTo{object->GetReferenceParam(), object2->GetReferenceParam()};

	if (cell)
	{
		param_MoveTo.push_back(RawParameter(object->GetNetworkPos(Axis_X) - object2->GetNetworkPos(Axis_X)));
		param_MoveTo.push_back(RawParameter(object->GetNetworkPos(Axis_Y) - object2->GetNetworkPos(Axis_Y)));
		param_MoveTo.push_back(RawParameter(object->GetNetworkPos(Axis_Z) - object2->GetNetworkPos(Axis_Z)));
	}

	Interface::ExecuteCommand("MoveTo", move(param_MoveTo), key);

	Interface::EndDynamic();
}

void Game::SetActorValue(FactoryObject reference, bool base, unsigned char index, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Interface::StartDynamic();

	if (base)
		Interface::ExecuteCommand("SetActorValue", ParamContainer{actor->GetReferenceParam(), RawParameter(API::RetrieveValue_Reverse(index)), RawParameter(actor->GetActorBaseValue(index))}, key);
	else
		Interface::ExecuteCommand("ForceActorValue", ParamContainer{actor->GetReferenceParam(), RawParameter(API::RetrieveValue_Reverse(index)), RawParameter(actor->GetActorValue(index))}, key);

	Interface::EndDynamic();
}

thread Game::SetActorSneaking(FactoryObject reference, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	SetRestrained(reference, false);

	NetworkID id = actor->GetNetworkID();
	thread t(AsyncTasks<AsyncPack, AsyncPack>,

	AsyncPack(async(launch::deferred, [](NetworkID id, unsigned int key)
	{
		try
		{
			FactoryObject reference = GameFactory::GetObject(id);
			Actor* actor = vaultcast<Actor>(reference);

			Interface::StartDynamic();

			Interface::ExecuteCommand("SetForceSneak", ParamContainer{actor->GetReferenceParam(), RawParameter(actor->GetActorSneaking())}, key);

			Interface::EndDynamic();
		}
		catch (...) {}
	}, id, key), chrono::milliseconds(20)),

	AsyncPack(async(launch::deferred, [](NetworkID id)
	{
		try
		{
			FactoryObject reference = GameFactory::GetObject(id);
			SetRestrained(reference, true);
		}
		catch (...) {}
	}, id), chrono::milliseconds(100)));

	return t;
}

thread Game::SetActorAlerted(FactoryObject reference, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	// really need to introduce restrained state in Actor class

	SetRestrained(reference, false);

	NetworkID id = actor->GetNetworkID();
	thread t(AsyncTasks<AsyncPack, AsyncPack>,

	AsyncPack(async(launch::deferred, [](NetworkID id, unsigned int key)
	{
		try
		{
			FactoryObject reference = GameFactory::GetObject(id);
			Actor* actor = vaultcast<Actor>(reference);

			Interface::StartDynamic();

			Interface::ExecuteCommand("SetAlert", ParamContainer{actor->GetReferenceParam(), RawParameter(actor->GetActorAlerted())}, key);

			Interface::EndDynamic();
		}
		catch (...) {}
	}, id, key), chrono::milliseconds(20)),

	AsyncPack(async(launch::deferred, [](NetworkID id)
	{
		try
		{
			FactoryObject reference = GameFactory::GetObject(id);
			SetRestrained(reference, true);
		}
		catch (...) {}
	}, id), chrono::milliseconds(100)));

	return t;
}

void Game::SetActorMovingAnimation(FactoryObject reference, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("PlayGroup", ParamContainer{actor->GetReferenceParam(), RawParameter(API::RetrieveAnim_Reverse(actor->GetActorMovingAnimation())), RawParameter(true)}, key);

	Interface::EndDynamic();
}

void Game::KillActor(FactoryObject reference, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("Kill", ParamContainer{actor->GetReferenceParam()}, key);

	Interface::EndDynamic();
}

void Game::AddItem(vector<FactoryObject> reference, unsigned int key)
{
	Item* item = vaultcast<Item>(reference[1]);

	if (!item)
		throw VaultException("Object with reference %08X is not an Item", (*reference[1])->GetReference());

	AddItem(reference[0], item->GetBase(), item->GetItemCount(), item->GetItemCondition(), item->GetItemSilent(), key);
}

void Game::AddItem(FactoryObject reference, unsigned int baseID, unsigned int count, double condition, bool silent, unsigned int key)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("AddItemHealthPercent", ParamContainer{container->GetReferenceParam(), RawParameter(baseID), RawParameter(count), RawParameter(condition / 100), RawParameter(silent)}, key);

	Interface::EndDynamic();
}

void Game::RemoveItem(vector<FactoryObject> reference, unsigned int key)
{
	Item* item = vaultcast<Item>(reference[1]);

	if (!item)
		throw VaultException("Object with reference %08X is not an Item", (*reference[1])->GetReference());

	RemoveItem(reference[0], item->GetBase(), item->GetItemCount(), item->GetItemSilent(), key);
}

void Game::RemoveItem(FactoryObject reference, unsigned int baseID, unsigned int count, bool silent, unsigned int key)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("RemoveItem", ParamContainer{container->GetReferenceParam(), RawParameter(baseID), RawParameter(count), RawParameter(silent)}, key);

	Interface::EndDynamic();
}

void Game::RemoveAllItems(FactoryObject reference, unsigned int key)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("RemoveAllItems", ParamContainer{container->GetReferenceParam()}, key);

	Interface::EndDynamic();
}

void Game::EquipItem(vector<FactoryObject> reference, unsigned int key)
{
	Item* item = vaultcast<Item>(reference[1]);

	if (!item)
		throw VaultException("Object with reference %08X is not an Item", (*reference[1])->GetReference());

	EquipItem(reference[0], item->GetBase(), item->GetItemSilent(), item->GetItemStick(), key);
}

void Game::EquipItem(FactoryObject reference, unsigned int baseID, bool silent, bool stick, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("EquipItem", ParamContainer{actor->GetReferenceParam(), RawParameter(baseID), RawParameter(stick), RawParameter(silent)}, key);

	Interface::EndDynamic();
}

void Game::UnequipItem(vector<FactoryObject> reference, unsigned int key)
{
	Item* item = vaultcast<Item>(reference[1]);

	if (!item)
		throw VaultException("Object with reference %08X is not an Item", (*reference[1])->GetReference());

	UnequipItem(reference[0], item->GetBase(), item->GetItemSilent(), item->GetItemStick(), key);
}

void Game::UnequipItem(FactoryObject reference, unsigned int baseID, bool silent, bool stick, unsigned int key)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Interface::StartDynamic();

	Interface::ExecuteCommand("UnequipItem", ParamContainer{actor->GetReferenceParam(), RawParameter(baseID), RawParameter(stick), RawParameter(silent)}, key);

	Interface::EndDynamic();
}

void Game::net_SetPos(FactoryObject reference, double X, double Y, double Z)
{
	Object* object = vaultcast<Object>(reference);
	bool result = ((bool) object->SetNetworkPos(Axis_X, X) | (bool) object->SetNetworkPos(Axis_Y, Y) | (bool) object->SetNetworkPos(Axis_Z, Z));

	if (result && object->GetEnabled())
	{
		Actor* actor = vaultcast<Actor>(object);   // maybe we should consider items, too (they have physics)

		if (actor == NULL || (!actor->IsNearPoint(object->GetNetworkPos(Axis_X), object->GetNetworkPos(Axis_Y), object->GetNetworkPos(Axis_Z), 200.0) && actor->GetActorMovingAnimation() == AnimGroup_Idle) || actor->IsActorJumping())
			SetPos(reference);
	}
}

void Game::net_SetAngle(FactoryObject reference, unsigned char axis, double value)
{
	Object* object = vaultcast<Object>(reference);
	bool result = (bool) object->SetAngle(axis, value);

	if (result && object->GetEnabled())
		SetAngle(reference);
}

void Game::net_SetCell(vector<FactoryObject> reference, unsigned int cell)
{
	Object* object = vaultcast<Object>(reference[0]);
	Player* self = vaultcast<Player>(reference[1]);

	if (!self)
		throw VaultException("Object with reference %08X is not a Player", (*reference[1])->GetReference());

	object->SetNetworkCell(cell);

	if (object != self)
	{
		if (object->GetNetworkCell() != self->GetGameCell())
		{
			if (object->SetEnabled(false))
				ToggleEnabled(reference[0]);
		}
		else
		{
			if (object->SetEnabled(true))
				ToggleEnabled(reference[0]);
		}
	}
}

void Game::net_ContainerUpdate(FactoryObject& reference, ContainerDiff diff)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	Lockable* result;

	// cleaner solution here

	NetworkID id = container->GetNetworkID();
	GameFactory::LeaveReference(reference);

	while (!(result = container->getLock()));

	reference = GameFactory::GetObject(id);

	unsigned int key = result->Lock();

	GameDiff gamediff = container->ApplyDiff(diff);

	for (pair<unsigned int, Diff>& diff : gamediff)
	{
		if (diff.second.equipped)
		{
			if (diff.second.equipped > 0)
				EquipItem(reference, diff.first, diff.second.silent, diff.second.stick, result->Lock());
			else if (diff.second.equipped < 0)
				UnequipItem(reference, diff.first, diff.second.silent, diff.second.stick, result->Lock());
		}
		else if (diff.second.count > 0)
			AddItem(reference, diff.first, diff.second.count, diff.second.condition, diff.second.silent, result->Lock());
		else if (diff.second.count < 0)
			RemoveItem(reference, diff.first, abs(diff.second.count), diff.second.silent, result->Lock());

		//else
		// new condition, can't handle yet
	}

	result->Unlock(key);
}

void Game::net_SetActorValue(FactoryObject reference, bool base, unsigned char index, double value)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Lockable* result;

	if (base)
		result = actor->SetActorBaseValue(index, value);
	else
		result = actor->SetActorValue(index, value);

	if (result)
		SetActorValue(reference, base, index, result->Lock());
}

void Game::net_SetActorState(FactoryObject reference, unsigned char index, unsigned char moving, bool alerted, bool sneaking)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Lockable* result;

	result = actor->SetActorMovingXY(moving);

	if (result && actor->GetEnabled())
		SetAngle(reference);

	result = actor->SetActorAlerted(alerted);

	if (result && actor->GetEnabled())
		SetActorAlerted(reference, result->Lock()).detach();

	result = actor->SetActorSneaking(sneaking);

	if (result && actor->GetEnabled())
		SetActorSneaking(reference, result->Lock()).detach();

	result = actor->SetActorMovingAnimation(index);

	if (result && actor->GetEnabled())
	{
		SetActorMovingAnimation(reference, result->Lock());

		if (index == AnimGroup_Idle)
			SetPos(reference);
	}
}

void Game::net_SetActorDead(FactoryObject& reference, bool dead)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	Lockable* result;

	result = actor->SetActorDead(dead);

	if (result)
	{
		if (dead)
			KillActor(reference, result->Lock());
		else if (actor->GetReference() != PLAYER_REFERENCE)
		{
			RemoveObject(reference);
			actor->SetReference(0x00000000);
			NewActor(reference);
		}
		else
		{
			NetworkID id = actor->GetNetworkID();
			GameFactory::LeaveReference(reference);
			Game::LoadGame();
			Game::LoadEnvironment();

			Network::Queue(NetworkResponse{Network::CreateResponse(
				PacketFactory::CreatePacket(ID_UPDATE_DEAD, id, dead),
				HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
			});
		}
	}
}

void Game::GetPos(FactoryObject reference, unsigned char axis, double value)
{
	Object* object = vaultcast<Object>(reference);
	bool result = (bool) object->SetGamePos(axis, value);

	if (result && object->GetReference() == PLAYER_REFERENCE)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_POS, object->GetNetworkID(), object->GetGamePos(Axis_X), object->GetGamePos(Axis_Y), object->GetGamePos(Axis_Z)),
			HIGH_PRIORITY, RELIABLE_SEQUENCED, CHANNEL_GAME, server)
		});
}

void Game::GetAngle(FactoryObject reference, unsigned char axis, double value)
{
	Object* object = vaultcast<Object>(reference);
	bool result = (bool) object->SetAngle(axis, value);

	if (result)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_ANGLE, object->GetNetworkID(), axis, value),
			HIGH_PRIORITY, RELIABLE_SEQUENCED, CHANNEL_GAME, server)
		});
}

void Game::GetParentCell(vector<FactoryObject> reference, unsigned int cell)
{
	Object* object = vaultcast<Object>(reference[0]);
	Player* self = vaultcast<Player>(reference[1]);

	if (!self)
		throw VaultException("Object with reference %08X is not a Player", (*reference[1])->GetReference());

	if (object != self)
	{
		if (self->GetGameCell() == object->GetNetworkCell() && object->GetGameCell() != object->GetNetworkCell())
		{
			if (object->SetEnabled(true))
				ToggleEnabled(reference[0]);

			Lockable* result = object->SetGameCell(self->GetGameCell());

			if (result)
				MoveTo(reference, true, result->Lock());

			SetAngle(reference[0]);
		}
	}

	bool result = (bool) object->SetGameCell(cell);

	if (object != self)
	{
		if (object->GetNetworkCell() != self->GetGameCell())
		{
			if (object->SetEnabled(false))
				ToggleEnabled(reference[0]);
		}
		else
		{
			if (object->SetEnabled(true))
				ToggleEnabled(reference[0]);
		}
	}

	if (result && object == self)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_CELL, object->GetNetworkID(), cell),
			HIGH_PRIORITY, RELIABLE_SEQUENCED, CHANNEL_GAME, server)
		});
}

void Game::GetDead(vector<FactoryObject> reference, bool dead)
{
	Actor* actor = vaultcast<Actor>(reference[0]);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference[0])->GetReference());

	Player* self = vaultcast<Player>(reference[1]);

	if (!self)
		throw VaultException("Object with reference %08X is not a Player", (*reference[1])->GetReference());

	/*if (actor != self && !self->GetActorAlerted())
	{
	    // "bug death"

	    return;
	}*/

	bool result;

	result = (bool) actor->SetActorDead(dead);

	if (result)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_DEAD, actor->GetNetworkID(), dead),
			HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
		});
}

void Game::GetActorValue(FactoryObject reference, bool base, unsigned char index, double value)
{
	Actor* actor = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	bool result;

	if (base)
		result = (bool) actor->SetActorBaseValue(index, value);

	else
		result = (bool) actor->SetActorValue(index, value);

	if (result)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_VALUE, actor->GetNetworkID(), base, index, value),
			HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
		});
}

void Game::GetActorState(FactoryObject reference, unsigned char index, unsigned char moving, bool alerted, bool sneaking)
{
	Actor* actor  = vaultcast<Actor>(reference);

	if (!actor)
		throw VaultException("Object with reference %08X is not an Actor", (*reference)->GetReference());

	bool result;

	if (index == 0xFF)
		index = AnimGroup_Idle;

	result = ((bool) actor->SetActorMovingAnimation(index) | (bool) actor->SetActorMovingXY(moving) | (bool) actor->SetActorAlerted(alerted) | (bool) actor->SetActorSneaking(sneaking));

	if (result)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_STATE, actor->GetNetworkID(), index, moving, alerted, sneaking),
			HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
		});
}

void Game::GetControl(FactoryObject reference, unsigned char control, unsigned char key)
{
	Player* player = vaultcast<Player>(reference);

	if (!player)
		throw VaultException("Object with reference %08X is not a Player", (*reference)->GetReference());

	bool result;

	result = (bool) player->SetPlayerControl(control, key);

	if (result)
		Network::Queue(NetworkResponse{Network::CreateResponse(
			PacketFactory::CreatePacket(ID_UPDATE_CONTROL, player->GetNetworkID(), control, key),
			HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
		});
}

void Game::ScanContainer(FactoryObject reference, vector<unsigned char>& data)
{
	Container* container = vaultcast<Container>(reference);

	if (!container)
		throw VaultException("Object with reference %08X is not a Container", (*reference)->GetReference());

	Lockable* result;

	if ((result = container->getLock()))
	{
		unsigned int key = result->Lock();

#pragma pack(push, 1)
		struct ItemInfo
		{
			unsigned int baseID;
			unsigned int amount;
			unsigned int equipped;
			double condition;
		};
#pragma pack(pop)

		ItemInfo* items = reinterpret_cast<ItemInfo*>(&data[0]);
		unsigned int count = data.size() / sizeof(ItemInfo);

		FactoryObject _temp = GameFactory::GetObject(GameFactory::CreateInstance(ID_CONTAINER, 0x00000000));
		Container* temp = vaultcast<Container>(_temp);

		for (unsigned int i = 0; i < count; ++i)
		{
			FactoryObject _item = GameFactory::GetObject(GameFactory::CreateInstance(ID_ITEM, items[i].baseID));
			Item* item = vaultcast<Item>(_item);
			item->SetItemCount(items[i].amount);
			item->SetItemEquipped((bool) items[i].equipped);
			item->SetItemCondition(items[i].condition);
			temp->AddItem(item->GetNetworkID());
		}

		ContainerDiff diff = container->Compare(temp->GetNetworkID());

		if (!diff.first.empty() || !diff.second.empty())
		{
			Network::Queue(NetworkResponse{Network::CreateResponse(
				PacketFactory::CreatePacket(ID_UPDATE_CONTAINER, container->GetNetworkID(), &diff),
				HIGH_PRIORITY, RELIABLE_ORDERED, CHANNEL_GAME, server)
			});

			container->ApplyDiff(diff);
		}

		GameFactory::DestroyInstance(_temp);

		result->Unlock(key);
	}
}
