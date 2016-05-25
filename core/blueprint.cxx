#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <limits>
#include <vector>
#include <algorithm>
#include <fmt/format.h>
#include <arpa/inet.h>
#include "3p/pipes/pipes.hxx"
#include "core/blueprint.hxx"
#include "core/util.hxx"
#include "core/topo.hxx"

using std::runtime_error;
using std::out_of_range;
using std::unordered_map;
using std::string;
using std::make_pair;
using std::array;
using std::vector;
using std::to_string;
using std::pair;
using std::sort;
using std::find_if;
using std::ostream;

using namespace marina;
using namespace pipes;

using ull = unsigned long long;

// internal data structures ----------------------------------------------------
namespace marina
{

  struct Blueprint_
  {
    Blueprint_(string name) 
      : name{name},
        id{generate_guid()}
    {}

    string name, id;
    unordered_map<string, Network> networks;
    unordered_map<string, Computer> computers;
    vector<Link> links;
  };
  
  struct Network_
  {
    Network_(string name) : name{name} {} 

    string name;
    Bandwidth bandwidth{100_mbps};
    Latency latency{0_ms};
    IpV4Address ipv4space{"10.10.0.0", 16};
    //unordered_map<string, Interface> interfaces;
    string guid =  generate_mac();
    vector<Neighbor> connections;
    Network::EmbeddingInfo einfo;
  };

  struct Computer_
  {
    Computer_(string name) : name{name} {}

    string name;
    string os{"ubuntu-server-15.10"};
    Memory memory{4_gb}, disk{10_gb};
    size_t cores{2};
    Embedding embedding;

    unordered_map<string, Interface> interfaces;
  };

  struct Interface_
  {
    Interface_(string name) 
      : name{name} 
    {
      mac = generate_mac();
    }

    string name, mac;
    Latency latency{0_ms};
    Interface::EmbeddingInfo einfo;
    Bandwidth capacity{1_gbps};
  };

}

// Blueprint -------------------------------------------------------------------
Blueprint::Blueprint(string name)
  : _{new Blueprint_{name}}
{}

string Blueprint::name() const { return _->name; }
Blueprint & Blueprint::name(string name)
{
  _->name = name;
  return *this;
}

string Blueprint::id() const { return _->id; }

Network Blueprint::network(string name) 
{ 
  return _->networks.emplace(make_pair(name, name)).first->second; 
}

Computer Blueprint::computer(string name) 
{ 
  return _->computers.emplace(make_pair(name, name)).first->second;
}

vector<Computer> Blueprint::computers() const
{
  vector<Computer> cs;
  cs.reserve(_->computers.size());
  for(auto & p : _->computers) cs.push_back(p.second);
  return cs;
}

vector<Network> Blueprint::networks() const
{
  vector<Network> ns;
  ns.reserve(_->computers.size());
  for(auto & p : _->networks) ns.push_back(p.second);
  return ns;
}


vector<Network> Blueprint::connectedNetworks(const Computer c)
{
  vector<Network> result;
  for(const auto & p : c.interfaces())
  {
    const Interface & i = p.second;
    for(const Link & l : _->links)
    {
      if(l.endpoints[0] == i.mac())
      {
        Network n = getNetworkById(l.endpoints[1]);
        //TODO gross make this a set
        if(find_if(result.begin(), result.end(), [&n](const Network & x){
              return n.name() == x.name();
              }) == result.end())
        {
          result.push_back(n); 
        }
      }
      if(l.endpoints[1] == i.mac())
      {
        Network n = getNetworkById(l.endpoints[0]);
        if(find_if(result.begin(), result.end(), [&n](const Network & x){
              return n.name() == x.name();
              }) == result.end())
        {
          result.push_back(n); 
        }
      }
    }
  }
  return result;
}
     
/*
vector<Computer> Blueprint::connectedComputers(const Network n)
{
  vector<Computer> result;

  for(const Link & l : _->links)
  {
    if(l.endpoints[0] == n.guid())
    {
      Computer c = getComputerByMac(l.endpoints[1]);

    }
  }

  return result;
}
*/

Computer & Blueprint::getComputer(string name) const 
{ 
  return _->computers.at(name); 
}

Network & Blueprint::getNetwork(string name) const 
{ 
  return _->networks.at(name); 
}

Network Blueprint::getNetworkById(string guid) const
{
  for(Network n : networks())
  {
    if(n.guid() == guid) return n;
  }
  throw out_of_range{"network with id " + guid + " not found"};
}

Computer Blueprint::getComputerByMac(string mac) const
{
  for(Computer c : computers())
  {
    for(const auto & i : c.interfaces())
    {
      if(i.second.mac() == mac)
      {
        return c;
      }
    }
  }
  throw out_of_range{"computer with mac " + mac + " not found"};
}

void Blueprint::removeComputer(string name)
{
  _->computers.erase(name);  
}

void Blueprint::removeNetwork(string name)
{
  _->networks.erase(name);  
}


Json Blueprint::json() const
{
  Json j;
  j["name"] = name();
  j["networks"] = jtransform(_->networks);
  j["computers"] = jtransform(_->computers);
  j["links"] = jtransform(_->links);
  j["id"] = _->id;
  return j;
}

const vector<Link> & Blueprint::links() const
{
  return _->links;
}

void Blueprint::connect(Interface i, Network n)
{
  //_->links.push_back({i, n.add_ifx()});
  _->links.push_back({i.mac(), n.guid()});

  n._->connections.push_back(
    Neighbor{
      Neighbor::Kind::Computer,
      i.mac()//,
      //*this
    }
  );
}

void Blueprint::connect(Network a, Network b)
{
  //_->links.push_back({a.add_ifx(), b.add_ifx()});
  _->links.push_back({a.guid(), b.guid()});

  a._->connections.push_back(
    Neighbor{
      Neighbor::Kind::Network,
      b.guid()//,
     // *this
    }
  );
  
  b._->connections.push_back(
    Neighbor{
      Neighbor::Kind::Network,
      a.guid()//,
    //  *this
    }
  );

}

Blueprint Blueprint::localEmbedding(string host_id)
{
  Blueprint b{name()};

  for(Computer c : computers())
  {
    if(c.embedding().host == host_id)
    {
      b._->computers.insert_or_assign(c.name(), c);

      for(auto i : c.interfaces())
      {
        for(const Link & l : _->links)
        {
          if(l.endpoints[0] == i.second.mac())
          {
            b._->links.push_back(l);

            Network n = getNetworkById(l.endpoints[1]);
            b._->networks.insert_or_assign(n.name(), n);
          }
          if(l.endpoints[1] == i.second.mac())
          {
            b._->links.push_back(l);

            Network n = getNetworkById(l.endpoints[0]);
            b._->networks.insert_or_assign(n.name(), n);
          }
        }
      }
    }
  }

  return b;
}

Blueprint Blueprint::clone() const
{
  Blueprint m{name()};
  for(auto x : _->networks)
    m._->networks.insert_or_assign(x.first, x.second.clone());

  for(auto x : _->computers)
    m._->computers.insert_or_assign(x.first, x.second.clone());

  m._->links = _->links; //not a pointer based data structure
  return m;
}

auto extract(Json j, string tag, string context)
{
  try { return j.at(tag); }
  catch(...) 
  { 
    throw out_of_range{"error extracting " + context+":"+tag};
  }
}

Blueprint Blueprint::fromJson(Json j)
{
  string name = extract(j, "name", "blueprint");
  Blueprint bp{name};
  bp._->id = extract(j, "id", "blueprint");

  Json computers = extract(j, "computers", "blueprint");
  for(Json & cj : computers)
  {
    Computer c = Computer::fromJson(cj);
    bp._->computers.insert_or_assign(c.name(), c);
  }

  Json networks = extract(j, "networks", "blueprint");
  for(Json & nj : networks)
  {
    Network n = Network::fromJson(nj);
    bp._->networks.insert_or_assign(n.name(), n);
  }

  Json links = extract(j, "links", "blueprint");
  for(Json & lj : links)
  {
    Link l = Link::fromJson(lj);
    bp._->links.push_back(l);
  }

  return bp;
}

bool marina::operator== (const Blueprint &a, const Blueprint &b)
{
  if(a.name() != b.name()) return false;

  vector<Computer> acs = a.computers(),
                   bcs = b.computers();

  if(acs.size() != bcs.size()) return false;

  auto compare = [](auto &xs, auto &ys)
  {
    auto sorter =
      [](const auto &x, const auto &y) { return x.name() < y.name(); };

    sort(xs.begin(), xs.end(), sorter);
    sort(ys.begin(), ys.end(), sorter);

    for(size_t i=0; i<xs.size(); ++i)
    {
      if(xs[i] != ys[i]) return false;
    }

    return true;
  };

  if(!compare(acs, bcs)) return false;

  vector<Network> ans = a.networks(),
                  bns = b.networks();

  if(ans.size() != bns.size()) return false;
  if(!compare(ans, bns)) return false;
  

  return true;
}

bool marina::operator!= (const Blueprint &a, const Blueprint &b)
{
  return !(a == b);
}

//Neighbor ---------------------------------------------------------------------
Neighbor::Neighbor(Kind k, std::string id/*, Blueprint bp*/) 
  : kind{k}, 
    id{id}//,
    //bp{bp}
{}

Neighbor Neighbor::fromJson(Json j)
{
  string kind = extract(j, "kind", "neighbor");
  string id = extract(j, "id", "neighbor");

  Kind kind_;
  if(kind == "Network") kind_ = Kind::Network;
  else if(kind == "Computer") kind_ = Kind::Computer;
  else throw runtime_error{fmt::format("unknown neighbor kind '{}'", kind)};

  return Neighbor{kind_, id};
}

Json Neighbor::json() const
{
  Json j;
  switch(kind)
  {
    case Kind::Network: j["kind"] = "Network";
    case Kind::Computer: j["kind"] = "Computer";
  }
  j["id"] = id;

  return j;
}


// Bandwidth -------------------------------------------------------------------
Bandwidth::Bandwidth(size_t size, Unit unit)
  : size{size}, unit{unit}
{}

Bandwidth::Unit Bandwidth::parseUnit(string s)
{
    if(s == "bps") return Bandwidth::Unit::BPS;
    if(s == "kbps") return Bandwidth::Unit::KBPS;
    if(s == "mbps") return Bandwidth::Unit::MBPS;
    if(s == "gbps") return Bandwidth::Unit::GBPS;
    if(s == "tbps") return Bandwidth::Unit::TBPS;

    throw runtime_error{s + " is not a valid bandwidth"};
}

Bandwidth Bandwidth::fromJson(Json j)
{
  size_t size = extract(j, "value", "bandwidth");
  string unit = extract(j, "unit", "bandwidth");

  return Bandwidth{size, parseUnit(unit)};
}

Json Bandwidth::json() const
{
  Json j;
  j["value"] = size;
  j["unit"] = to_string(unit);
  return j;
}

Bandwidth marina::operator "" _bps(ull x) { return {x, Bandwidth::Unit::BPS }; }
Bandwidth marina::operator "" _kbps(ull x){ return {x, Bandwidth::Unit::KBPS}; }
Bandwidth marina::operator "" _mbps(ull x){ return {x, Bandwidth::Unit::MBPS}; }
Bandwidth marina::operator "" _gbps(ull x){ return {x, Bandwidth::Unit::GBPS}; }
Bandwidth marina::operator "" _tbps(ull x){ return {x, Bandwidth::Unit::TBPS}; }

string std::to_string(Bandwidth::Unit x)
{
  switch(x)
  {
    case Bandwidth::Unit::BPS: return "bps";
    case Bandwidth::Unit::KBPS: return "kbps";
    case Bandwidth::Unit::MBPS: return "mbps";
    case Bandwidth::Unit::GBPS: return "gbps";
    case Bandwidth::Unit::TBPS: return "tbps";
  }
}

bool marina::operator == (const Bandwidth &a, const Bandwidth &b)
{
  return a.megabits() == b.megabits();
}

bool marina::operator != (const Bandwidth &a, const Bandwidth &b)
{
  return !(a == b);
}

// Latency ---------------------------------------------------------------------
Latency::Latency(size_t size, Unit unit)
  : size{size}, unit{unit}
{}

Latency Latency::fromJson(Json j)
{
  size_t size = extract(j, "value", "latency");
  string unit = extract(j, "unit", "latency");

  return Latency(size, parseUnit(unit));
}

Json Latency::json() const
{
  Json j;
  j["value"] = size;
  j["unit"] = to_string(unit);
  return j;
}

Latency::Unit Latency::parseUnit(string s)
{
  if(s == "ns") return Latency::Unit::NS;
  if(s == "us") return Latency::Unit::US;
  if(s == "ms") return Latency::Unit::MS;
  if(s == "s") return Latency::Unit::S;

  throw runtime_error{s + " is not a valuid time unit"};
}

string std::to_string(Latency::Unit x)
{
  switch(x)
  {
    case Latency::Unit::NS: return "ns";
    case Latency::Unit::US: return "us";
    case Latency::Unit::MS: return "ms";
    case Latency::Unit::S:  return "s" ;
  }
}

Latency marina::operator "" _ns(ull x) { return {x, Latency::Unit::NS}; }
Latency marina::operator "" _us(ull x) { return {x, Latency::Unit::US}; } 
Latency marina::operator "" _ms(ull x) { return {x, Latency::Unit::MS}; } 
Latency marina::operator "" _s(ull x)  { return {x, Latency::Unit::S }; } 

double Latency::toMS() const
{
  switch(unit)
  {
    case Latency::Unit::NS: return size / 1e6;
    case Latency::Unit::US: return size / 1e3;
    case Latency::Unit::MS: return size;
    case Latency::Unit::S:  return size * 1e3 ;
  }
}

//TODO do better than this
bool marina::operator == (const Latency &a, const Latency &b)
{
  return fabs(a.toMS() - b.toMS()) < std::numeric_limits<double>::epsilon();
}

bool marina::operator != (const Latency &a, const Latency &b)
{
  return !(a == b);
}

// IpV4Address -----------------------------------------------------------------

IpV4Address::IpV4Address(const string & addr, uint32_t mask)
  : mask_{mask}
{
  int err = inet_pton(AF_INET, addr.c_str(), &addr_);
  if(err != 1) throw runtime_error{"bad ipv4 address"};
}

string IpV4Address::addrStr() const
{
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr_, buf, INET_ADDRSTRLEN);
  return string{buf};
}

string IpV4Address::cidr() const
{
  return fmt::format("{}/{}", addrStr(), mask_);
}

uint32_t IpV4Address::addr() const { return addr_; }
uint32_t IpV4Address::mask() const { return mask_; }

IpV4Address & IpV4Address::operator++(int)
{
  addr_ = htonl(ntohl(addr_) + 1);
  return *this;
}

IpV4Address & IpV4Address::operator--(int)
{
  addr_ = htonl(ntohl(addr_) - 1);
  return *this;
}

bool IpV4Address::netZero() const
{
  return ntohl(addr_) % 256 == 0;
}
  
ostream & marina::operator<<(ostream & o, const IpV4Address & a)
{
  o << a.cidr(); 
  return o;
}

IpV4Address marina::operator+(IpV4Address a, uint32_t x)
{
  IpV4Address b = a;
  b.addr_ = htonl(ntohl(a.addr_) + x);
  return b;
}

IpV4Address marina::operator-(IpV4Address a, uint32_t x)
{
  IpV4Address b = a;
  b.addr_ = htonl(ntohl(a.addr_) - x);
  return b;
}

IpV4Address IpV4Address::fromJson(Json j)
{
  uint64_t addr = extract(j, "addr", "ipv4address"),
           mask = extract(j, "mask", "ipv4address");

  IpV4Address a;
  a.addr_ = addr;
  a.mask_ = mask;

  return a;
}

Json IpV4Address::json() const
{
  Json j;

  j["addr"] = addr_;
  j["mask"] = mask_;

  return j;
}

// Network ---------------------------------------------------------------------
Network::Network(string name)
  : _{new Network_{name}}
{ }

Network Network::fromJson(Json j)
{
  string name = extract(j, "name", "network");
  Network n{name};
  n.latency(Latency::fromJson(extract(j, "latency", "network")));
  n.capacity(Bandwidth::fromJson(extract(j, "capacity", "network")));

  Json cnxs = extract(j, "connections", "network");
  for(const Json & cnx : cnxs)
  {
    Neighbor nbr = Neighbor::fromJson(cnx);
    n._->connections.push_back(nbr);
  }

  n._->guid = extract(j, "guid", "network");

  Json ipv4_ = extract(j, "ipv4", "network");
  n._->ipv4space = IpV4Address::fromJson(ipv4_);

  Json einfo = extract(j, "einfo", "network");
  n._->einfo.vni = extract(einfo, "vni", "einfo");

  return n;
}

string Network::name() const { return _->name; }
Network & Network::name(string name)
{
  _-> name = name;
  return *this;
}

const Bandwidth Network::capacity() const { return _->bandwidth; }
Network & Network::capacity(Bandwidth x)
{
  _->bandwidth = x;
  return *this;
}


const IpV4Address & Network::ipv4() const
{
  return _->ipv4space;
}

Network & Network::ipv4(string addr, uint32_t mask)
{
  _->ipv4space = IpV4Address{addr, mask};
  return *this;
}

const Latency Network::latency() const { return _->latency; }
Network & Network::latency(Latency x)
{
  _->latency = x;
  return *this;
}

string Network::guid() const
{
  return _->guid;
}

const vector<Neighbor> & Network::connections() const
{
  return _->connections;
}

Network::EmbeddingInfo & Network::einfo() const
{
  return _->einfo;
}

Json Network::json() const
{
  Json j;
  j["name"] = name();
  j["latency"] = latency().json();
  j["capacity"] = capacity().json();
  j["connections"] = jtransform(_->connections);
  j["ipv4"] = ipv4().json();
  j["guid"] = _->guid;
  j["einfo"]["vni"] = _->einfo.vni;
  return j;
}

Network Network::clone() const
{
  Network n{name()};
  n._->latency = _->latency;
  n._->bandwidth = _->bandwidth;
  n._->guid = _->guid;
  n._->einfo = _->einfo;
  return n;
}
  
bool marina::operator == (const Network &a, const Network &b)
{
  if(a.name() != b.name()) return false;
  if(a.capacity() != b.capacity()) return false;
  if(a.latency() != b.latency()) return false;
  if(a.guid() != b.guid()) return false;
  
  return true;
}

bool marina::operator != (const Network &a, const Network &b)
{
  return !(a == b);
}

// Mem -------------------------------------------------------------------------
Memory::Memory(size_t size, Unit unit)
  : size{size}, unit{unit}
{}

Memory Memory::fromJson(Json j)
{
  size_t size = extract(j, "value", "memory");
  string unit = extract(j, "unit", "memory");

  return Memory{size, parseUnit(unit)};
}

Json Memory::json() const
{
  Json j;
  j["value"] = size;
  j["unit"] = to_string(unit);
  return j;
}

Memory marina::operator "" _b(ull x)  { return {x, Memory::Unit::B }; }
Memory marina::operator "" _kb(ull x) { return {x, Memory::Unit::KB}; }
Memory marina::operator "" _mb(ull x) { return {x, Memory::Unit::MB}; }
Memory marina::operator "" _gb(ull x) { return {x, Memory::Unit::GB}; }
Memory marina::operator "" _tb(ull x) { return {x, Memory::Unit::TB}; }


size_t Memory::bytes() const
{
  switch(unit)
  {
    case Unit::B:  return size;
    case Unit::KB: return size << 10;
    case Unit::MB: return size << 20;
    case Unit::GB: return size << 30;
    case Unit::TB: return size << 40;
  }
}

size_t Memory::megabytes() const
{
  switch(unit)
  {
    case Unit::B:  return size >> 20;
    case Unit::KB: return size >> 10;
    case Unit::MB: return size;
    case Unit::GB: return size << 10;
    case Unit::TB: return size << 20;
  }
}

size_t Bandwidth::megabits() const
{
  switch(unit)
  {
    case Unit::BPS:  return size >> 20;
    case Unit::KBPS: return size >> 10;
    case Unit::MBPS: return size;
    case Unit::GBPS: return size << 10;
    case Unit::TBPS: return size << 20;
  }
}
bool marina::operator < (const Memory & a, const Memory & b)
{
  return a.bytes() < b.bytes();
}

bool marina::operator == (const Memory & a, const Memory & b)
{
  return a.bytes() == b.bytes();
}

bool marina::operator != (const Memory & a, const Memory & b)
{
  return !(a == b);
}

string std::to_string(Memory::Unit x)
{
  switch(x)
  {
    case Memory::Unit::B: return "b";
    case Memory::Unit::KB: return "kb";
    case Memory::Unit::MB: return "mb";
    case Memory::Unit::GB: return "gb";
    case Memory::Unit::TB: return "tb";
  }
}

string std::to_string(Memory m)
{
  return to_string(m.size) + to_string(m.unit);
}

Memory::Unit Memory::parseUnit(string s)
{
  if(s == "b") return Unit::B;
  if(s == "kb") return Unit::KB;
  if(s == "mb") return Unit::MB;
  if(s == "gb") return Unit::GB;
  if(s == "tb") return Unit::TB;

  throw runtime_error{s + " is not a valid memory unit"};
}

// Interface -------------------------------------------------------------------
Interface::Interface(string name)
  : _{new Interface_{name}}
{}

Interface Interface::fromJson(Json j)
{
  string name = extract(j, "name", "interface");
  Interface ifx{name};
  ifx.latency(Latency::fromJson(extract(j, "latency", "interface")))
     .capacity(Bandwidth::fromJson(extract(j, "capacity", "interface")));

  ifx._->mac = extract(j, "mac", "interface");
  
  Json einfo = extract(j, "einfo", "interface");
  Json ipaddr_v4 = extract(einfo, "ipv4", "interface:einfo");
  ifx.einfo().ipaddr_v4 = IpV4Address::fromJson(ipaddr_v4);

  return ifx;
}

string Interface::name() const { return _->name; }
Interface & Interface::name(string x)
{
  _->name = x;
  return *this;
}

const Latency Interface::latency() const { return _->latency; }
Interface & Interface::latency(Latency x)
{
  _->latency = x;
  return *this;
}

const Bandwidth Interface::capacity() const { return _->capacity; }
Interface & Interface::capacity(Bandwidth x)
{
  _->capacity = x;
  return *this;
}

string Interface::mac() const
{
  return _->mac;
}

Interface::EmbeddingInfo & Interface::einfo() const
{
  return _->einfo;
}

Json Interface::json() const
{
  Json j;
  j["name"] = name();
  j["latency"] = latency().json();
  j["capacity"] = capacity().json();
  j["mac"] = mac();
  j["einfo"]["ipv4"] = einfo().ipaddr_v4.json();
  return j;
}

Interface Interface::clone() const
{
  Interface i{name()};
  i._->mac = _->mac;
  i._->latency = _->latency;
  i._->capacity = _->capacity;
  return i;
}

bool marina::operator == (const Interface &a, const Interface &b)
{
  if(a.name() != b.name()) return false;
  if(a.latency() != b.latency()) return false;
  if(a.capacity() != b.capacity()) return false;
  //if(a.mac() != b.mac()) return false;

  return true;
}

bool marina::operator != (const Interface &a, const Interface &b)
{
  return !(a == b);
}

// HwSpec ----------------------------------------------------------------------
HwSpec::HwSpec(size_t proc, size_t mem, size_t net, size_t disk)
  : proc{proc}, mem{mem}, net{net}, disk{disk}
{}

double HwSpec::norm()
{
  return sqrt(proc*proc + mem*mem + net*net + disk*disk);
}

double HwSpec::inf_norm()
{
  return std::max({proc, mem, net, disk});
}

double HwSpec::neg_inf_norm()
{
  return std::min({proc, mem, net, disk});
}

HwSpec marina::operator+ (HwSpec a, HwSpec b)
{
  HwSpec x;
  x.proc = a.proc + b.proc;
  x.mem = a.mem + b.mem;
  x.net = a.net + b.net;
  x.disk = a.disk + b.disk;
  return x;
}

HwSpec marina::operator- (HwSpec a, HwSpec b)
{
  HwSpec x;
  x.proc = a.proc - b.proc;
  x.mem = a.mem - b.mem;
  x.net = a.net - b.net;
  x.disk = a.disk - b.disk;
  return x;
}

// Computer --------------------------------------------------------------------
Computer::Computer(string name)
  : _{new Computer_{name}}
{}

Computer Computer::fromJson(Json j)
{
  string name = extract(j, "name", "computer");
  Computer c{name};
  c.os(extract(j, "os", "computer"));
  c.memory(Memory::fromJson(extract(j, "memory", "computer")));
  c.cores(extract(j, "cores", "computer"));
  c.embedding(Embedding::fromJson(extract(j, "embedding", "computer")));

  Json ifxs = extract(j, "interfaces", "computer");
  for(const Json & ij : ifxs)
  {
    Interface ifx = Interface::fromJson(ij);
    c._->interfaces.insert_or_assign(ifx.name(), ifx);
  }

  return c;
}

string Computer::name() const { return _->name; }
Computer & Computer::name(string x)
{
  _->name = x;
  return *this;
}

string Computer::os() const { return _->os; }
Computer & Computer::os(string x)
{
  _->os = x;
  return *this;
}

const Memory Computer::memory() const { return _->memory; }
Computer & Computer::memory(Memory x)
{
  _->memory = x;
  return *this;
}

const Memory Computer::disk() const { return _->disk; }
Computer & Computer::disk(Memory x)
{
  _->disk = x;
  return *this;
}

size_t Computer::cores() const { return _->cores; }
Computer & Computer::cores(size_t x)
{
  _->cores = x;
  return *this;
}

Interface Computer::ifx(string x)
{
  try{ return _->interfaces.at(x); }
  catch(out_of_range &)
  {
    throw runtime_error {
      "Computer " + name() + " does not have an interface named " + x  };
  }
}

Computer & Computer::add_ifx(string name, Bandwidth capacity, Latency latency)
{
  _->interfaces.emplace(make_pair(name, name)).first->second
    .name(name)
    .capacity(capacity)
    .latency(latency);

  return *this;
}

Computer & Computer::remove_ifx(string name)
{
  _->interfaces.erase(name);
  return *this;
}

Embedding Computer::embedding() const { return _->embedding; }
Computer & Computer::embedding(Embedding e)
{
  _->embedding = e;
  return *this;
}

HwSpec Computer::hwspec() const
{
  HwSpec x;
  x.proc = cores();
  x.mem = memory().bytes();
  x.disk = disk().megabytes();
  x.net = _->interfaces 
    | map<vector>([](auto x){ return x.second.capacity().megabits(); })
    | reduce(plus);

  return x;
}

Json Computer::json() const
{
  Json j;
  j["name"] = name();
  j["os"] = os();
  j["memory"] = memory().json();
  j["cores"] = cores();
  j["interfaces"] = jtransform(_->interfaces);
  j["embedding"] = embedding().json();
  return j;
}

Computer Computer::clone() const
{
  Computer c{name()};
  c._->os = _->os;
  c._->memory = _->memory;
  c._->cores = _->cores;
  c._->disk = _->disk;
  c._->embedding = _->embedding;

  for(auto & p : _->interfaces)
  { 
    c._->interfaces.insert_or_assign(p.first, p.second.clone()); 
  }

  return c;
}

unordered_map<string, Interface> & Computer::interfaces() const 
{ 
  return _->interfaces; 
}

Interface Computer::getInterfaceByMac(string m) const
{
  auto i = 
    find_if(_->interfaces.begin(), _->interfaces.end(),
      [m](const auto p){ return p.second.mac() == m; }
    );

  if(i == _->interfaces.end()) 
    throw runtime_error{
      fmt::format("computer {} does not contain interface {}",
          name(), m)
    };

  return i->second;
}
  

bool marina::operator== (const Computer & a, const Computer & b)
{
  if(a.name() != b.name()) return false;
  if(a.os() != b.os()) return false;
  if(a.memory() != b.memory()) return false;
  if(a.disk() != b.disk()) return false;
  if(a.cores() != b.cores()) return false;

  for(auto p : a.interfaces())
  {
    auto i = b.interfaces().find(p.first);
    if(i == b.interfaces().end()) return false;
    if(p.second != i->second) return false;
  }

  return true;
}

bool marina::operator!= (const Computer & a, const Computer & b)
{
  return !(a == b);
}

// Link ------------------------------------------------------------------------
Link::Link(Interface a, Interface b)
  : endpoints{{a.mac(), b.mac()}}
{}

Link::Link(string a, string b)
  : endpoints{{a, b}}
{}

Link Link::fromJson(Json j)
{
  vector<string> eps = extract(j, "endpoints", "link");
  Link l;
  l.endpoints[0] = eps[0];
  l.endpoints[1] = eps[1];
  return l;
}

Json Link::json() const
{
  Json j;
  j["endpoints"] = endpoints;
  return j;
}
