#include "Window.h"

using namespace std;
using namespace RakNet;

Window::WindowChilds Window::childs;

Window::Window() : Reference(), parent(0), locked(false), visible(true)
{
	initialize();
}

Window::Window(const pDefault* packet) : Reference()
{
	initialize();

	NetworkID id;

	PacketFactory::Access<pTypes::ID_WINDOW_NEW>(packet, id, parent, label, pos, size, locked, visible, text);

	this->SetNetworkID(id);

	if (parent)
		childs[parent].emplace(id);
}

Window::~Window() noexcept
{
	if (parent)
		childs[parent].erase(this->GetNetworkID());
}

void Window::initialize()
{

}

void Window::CollectChilds(NetworkID root, vector<NetworkID>& dest)
{
	dest.emplace_back(root);

	for (const auto& child : childs[root])
		CollectChilds(child, dest);
}

void Window::SetParentWindow(Window* parent)
{
	NetworkID id = this->GetNetworkID();

	if (this->parent)
		childs[this->parent].erase(id);

	if (parent)
	{
		this->parent = parent->GetNetworkID();
		childs[this->parent].emplace(id);
	}
	else
		this->parent = 0;
}

bool Window::SetPos(double X, double Y, double offset_X, double offset_Y)
{
	if (X >= 0.0 && X <= 1.0 && Y >= 0.0 && Y <= 1.0)
	{
		pos = decltype(pos){X, Y, offset_X, offset_Y};
		return true;
	}

	return false;
}

bool Window::SetSize(double X, double Y, double offset_X, double offset_Y)
{
	if (X >= 0.0 && X <= 1.0 && Y >= 0.0 && Y <= 1.0)
	{
		size = decltype(size){X, Y, offset_X, offset_Y};
		return true;
	}

	return false;
}

pPacket Window::toPacket() const
{
	pPacket packet = PacketFactory::Create<pTypes::ID_WINDOW_NEW>(const_cast<Window*>(this)->GetNetworkID(), parent, label, pos, size, locked, visible, text);

	return packet;
}
