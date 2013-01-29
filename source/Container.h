#ifndef CONTAINER_H
#define CONTAINER_H

#include <map>
#include <list>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include "vaultmp.h"
#include "Data.h"
#include "Object.h"
#include "PacketFactory.h"

#ifdef VAULTMP_DEBUG
#include "Debug.h"
#endif

class Item;

class Container : public Object
{
		friend class GameFactory;
		friend class Item;

	public:
		struct Diff
		{
			signed int count;
			double condition;
			signed int equipped;
			bool silent;
			bool stick;

			Diff() : count(0), condition(0.00), equipped(0), silent(false), stick(false) {}
		};

		typedef std::pair<std::list<RakNet::NetworkID>, std::list<RakNet::NetworkID>> ContainerDiff;
		typedef std::pair<std::list<RakNet::NetworkID>, std::vector<pPacket>> NetDiff;
		typedef std::list<std::pair<unsigned int, Diff>> GameDiff;

	private:
		typedef std::pair<RakNet::NetworkID, std::unordered_map<RakNet::NetworkID, std::list<RakNet::NetworkID>>> StripCopy;

#ifdef VAULTMP_DEBUG
		static DebugInput<Container> debug;
#endif

		static bool Item_sort(RakNet::NetworkID id, RakNet::NetworkID id2);
		static bool Diff_sort(const std::pair<unsigned int, Diff>& diff, const std::pair<unsigned int, Diff>& diff2);

		std::list<RakNet::NetworkID> container;
		Value<bool> flag_Lock;

		StripCopy Strip() const;

		void initialize();

		Container(const Container&) = delete;
		Container& operator=(const Container&) = delete;

	protected:
		Container(unsigned int refID, unsigned int baseID);
		Container(const pDefault* packet);
		Container(pPacket&& packet);

	public:
		virtual ~Container();
#ifndef VAULTSERVER
		/**
		 * \brief Creates a Parameter containing a VaultFunctor initialized with the given flags
		 *
		 * Used to pass Container references matching the provided flags to the Interface
		 * Can also be used to pass data of a given Container to the Interface
		 */
		static FuncParameter CreateFunctor(unsigned int flags, RakNet::NetworkID id = 0);
#endif
		void AddItem(RakNet::NetworkID id);
		ContainerDiff AddItem(unsigned int baseID, unsigned int count, double condition, bool silent) const;
		void RemoveItem(RakNet::NetworkID id);
		ContainerDiff RemoveItem(unsigned int baseID, unsigned int count, bool silent) const;
		ContainerDiff RemoveAllItems() const;
		ContainerDiff EquipItem(unsigned int baseID, bool silent, bool stick) const;
		ContainerDiff UnequipItem(unsigned int baseID, bool silent, bool stick) const;

		ContainerDiff Compare(RakNet::NetworkID id) const;
		RakNet::NetworkID IsEquipped(unsigned int baseID) const;
		GameDiff ApplyDiff(ContainerDiff& diff);

		static ContainerDiff ToContainerDiff(const NetDiff& diff);
		static NetDiff ToNetDiff(const ContainerDiff& diff);
		static void FreeDiff(ContainerDiff& diff);

		Lockable* getLock();
		bool IsEmpty() const;
		unsigned int GetItemCount(unsigned int baseID = 0) const;
		const std::list<RakNet::NetworkID>& GetItemList() const;

#ifdef VAULTSERVER
		std::list<RakNet::NetworkID> GetItemTypes(const std::string& type) const;

		/**
		 * \brief Sets the Container's base ID
		 */
		virtual Lockable* SetBase(unsigned int baseID);
#endif

		void FlushContainer();
		RakNet::NetworkID Copy() const;

#ifdef VAULTMP_DEBUG
		void PrintContainer() const;
#endif

		/**
		 * \brief For network transfer
		 */
		virtual pPacket toPacket() const;
};

#ifndef VAULTSERVER
class ContainerFunctor : public ObjectFunctor
{
	public:
		ContainerFunctor(unsigned int flags, RakNet::NetworkID id) : ObjectFunctor(flags, id) {}
		virtual ~ContainerFunctor() {}

		virtual std::vector<std::string> operator()();
		virtual bool filter(FactoryObject<Reference>& reference);
};
#endif

#endif
