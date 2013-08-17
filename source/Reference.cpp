#include "Reference.h"
#include "Network.h"
#include "Utils.h"

using namespace std;
using namespace RakNet;

#ifdef VAULTMP_DEBUG
DebugInput<Reference> Reference::debug;
#endif

Reference::Reference()
{
	this->SetNetworkIDManager(Network::Manager());
}

Reference::~Reference() noexcept {}

/*
unsigned int Reference::ResolveIndex(unsigned int baseID)
{
	unsigned char idx = (unsigned char)(((unsigned int)(baseID & 0xFF000000)) >> 24);
	IndexLookup::iterator it = Mods.find(idx);

	if (it != Mods.end())
		return (baseID & 0x00FFFFFF) | (((unsigned int) it->second) << 24);

	return baseID;
}
*/

template<>
Lockable* Reference::SetObjectValue(Value<double>& dest, const double& value)
{
	if (Utils::DoubleCompare(dest.get(), value, 0.0001))
		return nullptr;

	if (!dest.set(value))
		return nullptr;

	return &dest;
}

#ifndef VAULTSERVER
void Reference::Enqueue(const function<void()>& task)
{
	tasks.push(task);
}

void Reference::Work()
{
	while (!tasks.empty())
	{
		tasks.front()();
		tasks.pop();
	}
}

void Reference::Release()
{
	while (!tasks.empty())
		tasks.pop();
}
#endif
