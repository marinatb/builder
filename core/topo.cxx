#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include "topo.hxx"
#include "util.hxx"
#include "3p/pipes/pipes.hxx"

using std::runtime_error;
using std::out_of_range;
using std::string;
using std::stringstream;
using std::unordered_set;
using std::hash;
using std::vector;
using std::array;
using std::set_difference;
using std::inserter;
using std::pair;
using std::unordered_map;
using std::remove_if;
using std::endl;
using namespace marina;
using namespace pipes;

size_t SwitchSetHash::operator() (const Switch & s) const
{ 
  return hash<string>{}(s.name()); 
}

size_t HostSetHash::operator() (const Host & c) const
{ 
  return hash<string>{}(c.name()); 
}

bool SwitchSetCMP::operator() (const Switch & a, const Switch & b) const
{
  SwitchSetHash h;
  return h(a) == h(b);
}

bool HostSetCMP::operator() (const Host & a, const Host & b) const
{
  HostSetHash h;
  return h(a) == h(b);
}

bool marina::operator== (const Switch & a, const Switch & b)
{
  if(a.name() != b.name()) return false;
  if(a.backplane() != b.backplane()) return false;
  if(a.allocatedBackplane() != b.allocatedBackplane()) return false;

  return true;
}

bool marina::operator!= (const Switch & a, const Switch & b)
{
  return !(a == b);
}


bool marina::operator== (const Host & a, const Host & b)
{
  if(a.name() != b.name()) return false;
  if(a.cores() != b.cores()) return false;
  if(a.memory() != b.memory()) return false;
  if(a.disk() != b.disk()) return false;
  
  for(auto p : a.interfaces())
  {
    auto i = b.interfaces().find(p.first);
    if(i == b.interfaces().end()) return false;
    if(p.second != i->second) return false;
  }

  return true;
}

bool marina::operator!= (const Host & a, const Host & b)
{
  return !(a == b);
}

namespace marina 
{

  /*
  struct Endpoint_
  {
    enum class Kind { Host, Switch };

    Endpoint(Kind kind, string name) : kind{kind}, name{name} {}
    Kind kind;
    string name;

    static Endpoint fromJson(Json j)
    {
      string ks = j.at("kind");
      Kind k;
      if(ks == "host") k = Kind::Host;
      else if(ks == "switch") k = Kind::Switch;
      else throw runtime_error{ks + " is not a valid endpoint kind"};

      return Endpoint{k, j.at("name")};
    }

    Json json() const
    {
      Json j;
      switch(kind)
      {
        case Kind::Host: j["kind"] = "host"; break;
        case Kind::Switch: j["kind"] = "switch"; break;
      }
      j["name"] = name;
      return j;
    }
  };
  */

  struct TbLink
  {
    /*
    TbLink(Switch a, Switch b, Bandwidth capacity)
      : endpoints{{
          {Endpoint::Kind::Switch, a.name()},
          {Endpoint::Kind::Switch, b.name()}
        }},
        capacity{capacity}
    {}
    */
    TbLink(Switch a, Switch b, Bandwidth capacity)
      : endpoints{{
          Endpoint{a.id()},
          Endpoint{b.id()}
        }},
        capacity{capacity}
    {}

    /*
    TbLink(Host a, Switch b, Bandwidth capacity)
      : endpoints{{
          Endpoint{Endpoint::Kind::Host, a.name()},
          Endpoint{Endpoint::Kind::Switch, b.name()}
        }},
        capacity{capacity}
    {}
    */
    //TODO remove capacity, not a property of link itself
    TbLink(pair<Host, Interface> a, Switch b, Bandwidth capacity)
      : endpoints{{
          Endpoint{a.first.id(), a.second.mac()},
          Endpoint{b.id()}
        }},
        capacity{capacity}
    {}

    TbLink(Endpoint a, Endpoint b, Bandwidth c)
      : endpoints{{a, b}},
        capacity{c}
    {}


    static TbLink fromJson(Json j)
    {

      Json eps = j.at("endpoints");
      Endpoint a = Endpoint::fromJson(eps.at(0));
      Endpoint b = Endpoint::fromJson(eps.at(1));
      Bandwidth c = Bandwidth::fromJson(j.at("capacity"));

      return TbLink{a, b, c};
    }

    array<Endpoint,2> endpoints;
    Bandwidth capacity;

    Json json() const
    {
      Json j;
      j["endpoints"] = jtransform(endpoints);
      j["capacity"] = capacity.json();
      return j;
    }
  };

  struct TestbedTopology_
  {
    TestbedTopology_(string name) : name{name} {}

    string name;

    TestbedTopology::SwitchMap switches;

    TestbedTopology::HostMap hosts;
    vector<TbLink> links;

    void removeEndpointLink(const Endpoint &);
  };

  struct Switch_
  {
    Switch_(string name) : name{name} {} 

    Uuid id;
    string name;
    Bandwidth backplane;

    vector<Network> networks;
  };

  struct Host_
  {
    Host_(string name) : host_comp{name} 
    {
      host_comp.add_ifx("ifx", 0_gbps);
    }

    Computer host_comp;
    vector<Computer> machines;
  };

}


// TestbedTopology -------------------------------------------------------------

TestbedTopology::TestbedTopology(string name) 
  : _{new TestbedTopology_{name}} 
{}

TestbedTopology TestbedTopology::fromJson(Json j)
{
  string name = j.at("name");
  TestbedTopology t{name};

  Json hosts = j.at("hosts");
  for(Json & hj : hosts)
  {
    Host h = Host::fromJson(hj);
    t._->hosts.insert_or_assign(h.id(), h);
  }

  Json switches = j.at("switches");
  for(Json & sj : switches)
  {
    Switch s = Switch::fromJson(sj);
    t._->switches.insert_or_assign(s.id(), s);
  }
  
  Json links = j.at("links");
  for(Json & lj : links)
  {
    TbLink l = TbLink::fromJson(lj);
    t._->links.push_back(l);
  }

  return t;
}

string TestbedTopology::name() const { return _->name; }
TestbedTopology & TestbedTopology::name(string name)
{
  _->name = name;
  return *this;
}

TestbedTopology::SwitchMap & TestbedTopology::switches() const 
{ 
  return _->switches; 
}

TestbedTopology::HostMap & TestbedTopology::hosts() const 
{ 
  return _->hosts; 
}

Switch TestbedTopology::sw(string name)
{
  Switch s{name};
  _->switches.insert_or_assign(s.id(), s);
  return s;
}

Switch TestbedTopology::getSw(string name)
{
  Switch s{name};
  auto i = find_if(switches().begin(), switches().end(),
      [name](const auto & x){ return x.second.name() == name; });
  if(i == _->switches.end())
    throw out_of_range{"switch "+name+" does not exist"};
  return i->second;
}

Host TestbedTopology::getHost(string name)
{
  auto i = find_if(hosts().begin(), hosts().end(),
      [name](const auto & h){ return h.second.name() == name; });
  if(i == _->hosts.end())
    throw out_of_range{"host "+name+" does not exist"};
  return i->second;
}

Host TestbedTopology::host(string name)
{
  Host c{name};
  _->hosts.insert_or_assign(c.id(), c);
  return c;
}

TestbedTopology::HostSet TestbedTopology::connectedHosts(const Switch s) const
{
  TestbedTopology::HostSet hs;
  Endpoint e{s.id()};

  for(TbLink & l : _->links)
  {
    //if(l.endpoints[0].name == s.name() && 
    //   l.endpoints[1].kind == Endpoint::Kind::Host)
    if(l.endpoints[0] == e)
    {
      auto i = hosts().find(l.endpoints[1].id);
      //auto i = find_if(hosts().begin(), hosts().end(),
      //    [&e,&l](const Host & h){ return h.id() == l.endpoints[1].id; });

      if(i != hosts().end()) hs.insert(i->second);
      //hs.insert(getHost(l.endpoints[1].name)); 
    }
    
    //if(l.endpoints[1].name == s.name() && 
    //   l.endpoints[0].kind == Endpoint::Kind::Host)
    if(l.endpoints[1] == e)
    {
      auto i = hosts().find(l.endpoints[0].id);
      //auto i = find_if(hosts().begin(), hosts().end(),
      //    [&e,&l](const Host & h){ return h.id() == l.endpoints[0].id; });
      
      if(i != hosts().end()) hs.insert(i->second);
    }
  }
  return hs;
}

TestbedTopology::SwitchSet TestbedTopology::connectedSwitches(const Host h) const
{
  TestbedTopology::SwitchSet sws;

  for(TbLink & l : _->links)
  {
    if(l.endpoints[0].id == h.id())
    {
      auto i = switches().find(l.endpoints[1].id);
      if(i != switches().end()) sws.insert(i->second);
    }
    if(l.endpoints[1].id == h.id())
    {
      auto i = switches().find(l.endpoints[2].id);
      if(i != switches().end()) sws.insert(i->second);
    }
  }
  return sws;
}

void TestbedTopology::connect(Switch a, Switch b, Bandwidth bw)
{
  _->links.push_back({a, b, bw});
}

void TestbedTopology::connect(pair<Host, Interface> a, Switch b, Bandwidth bw)
{
  _->links.push_back({a, b, bw});
}

Json TestbedTopology::json() const
{
  Json j;
  j["name"] = name();
  j["switches"] = jtransform(_->switches);
  j["hosts"] = jtransform(_->hosts);
  j["links"] = jtransform(_->links);
  return j;
}

//void TestbedTopology_::removeEndpointLink(Endpoint::Kind kind, string name)
void TestbedTopology_::removeEndpointLink(const Endpoint & e)
{
  links.erase(
    remove_if(links.begin(), links.end(),
      [&e](const TbLink & l){ 
        Endpoint a = l.endpoints[0],
                 b = l.endpoints[1];
        return a == e || b == e;
        //(a.kind == kind && a.name == name) ||
        //(b.kind == kind && b.name == name) ;
    }), links.end()
  );

}
      
void TestbedTopology::removeSw(string name)
{

  auto i = find_if(switches().begin(), switches().end(),
      [name](const auto & x) { return x.second.name() == name; });
  if(i == switches().end()) throw runtime_error{"unkown switch"};

  Endpoint e{i->first};
  _->removeEndpointLink(e);
}

void TestbedTopology::removeHost(string name)
{
  //auto i = _->hosts.erase(Host{name});
  //if(i == 0) return;
  //_->removeEndpointLink(Endpoint::Kind::Host, name);
  
  auto i = find_if(hosts().begin(), hosts().end(),
      [name](const auto & x) { return x.second.name() == name; });
  if(i == hosts().end()) throw runtime_error{"unkown host"};

  Endpoint e{i->first};
  _->removeEndpointLink(e);
}

TestbedTopology TestbedTopology::clone() const
{
  TestbedTopology t{name()};

  for(auto & h : _->hosts) 
    t._->hosts.insert_or_assign(h.first, h.second.clone());

  for(auto & s : _->switches) 
    t._->switches.insert_or_assign(s.first, s.second.clone());

  return t;
}

bool marina::operator == (const TestbedTopology & a, const TestbedTopology & b)
{
  if(a.name() != b.name()) return false;

  if(a.switches().size() != b.switches().size()) return false;
  for(const auto & x : a.switches())
  {
    auto i = b.switches().find(x.first);
    if(i == b.switches().end()) return false;
    if(i->second != x.second) return false;
  }
  
  if(a.hosts().size() != b.hosts().size()) return false;
  for(const auto & x : a.hosts())
  {
    auto i = b.hosts().find(x.first);
    if(i == b.hosts().end()) return false;
    if(i->second != x.second) return false;
  }

  return true;
}

bool marina::operator != (const TestbedTopology & a, const TestbedTopology & b)
{
  return !(a == b);
}

// Switch ----------------------------------------------------------------------

Switch::Switch(string name)
  : _{new Switch_{name}}
{}

const Uuid & Switch::id() const { return _->id; }

string Switch::name() const { return _->name; }
Switch & Switch::name(string name)
{
  _->name = name;
  return *this;
}

vector<Network> & Switch::networks() const
{
  return _->networks;
}

void Switch::removeNetwork(Uuid id)
{
  _->networks.erase(
    remove_if(_->networks.begin(), _->networks.end(),
      [&id](const Network & n)
      {
        return n.id() == id;
      }
    ),
    _->networks.end()
  );
}

Bandwidth Switch::backplane() const { return _->backplane; }
Switch & Switch::backplane(Bandwidth b)
{
  _->backplane = b;
  return *this;
}

Bandwidth Switch::allocatedBackplane() const 
{ 
  size_t x{0};
  for(const Network & n : _->networks)
  {
    x += n.capacity().megabits();
  }
  return Bandwidth{x, Bandwidth::Unit::MBPS};
}

Switch Switch::fromJson(Json j)
{
  string name = extract(j, "name", "switch");
  Switch s{name};
  s.backplane(Bandwidth::fromJson(extract(j, "backplane", "switch")));

  Json njs = extract(j, "networks", "switch");
  for(const Json & n : njs)
  {
    s._->networks.push_back(Network::fromJson(n));
  }

  return s;
}

Json Switch::json() const
{
  Json j;
  j["name"] = name();
  j["backplane"] = backplane().json();
  j["allocated-backplane"] = allocatedBackplane().json();
  j["networks"] = jtransform(_->networks);

  return j;
}

Switch Switch::clone() const
{
  Switch s{_->name};
  s._->backplane = _->backplane;
  s._->networks = _->networks
    | map([](auto x){ return x.clone(); });

  return s;
}

// Host ------------------------------------------------------------------------

Host::Host(string name)
  : _{new Host_{name}}
{}

const Uuid & Host::id() const { return _->host_comp.id(); }

string Host::name() const { return _->host_comp.name(); }
Host & Host::name(string name)
{
  _->host_comp.name(name);
  return *this;
}

const Memory Host::memory() const { return _->host_comp.memory(); }
Host & Host::memory(Memory m)
{
  _->host_comp.memory(m);
  return *this;
}

const Memory Host::disk() const { return _->host_comp.disk(); }
Host & Host::disk(Memory m)
{
  _->host_comp.disk(m);
  return *this;
}

size_t Host::cores() const { return _->host_comp.cores(); }
Host & Host::cores(size_t n)
{
  _->host_comp.cores(n);
  return *this;
}

Interface Host::ifx(string name)
{
  return _->host_comp.ifx(name);
}

Host & Host::add_ifx(string name, Bandwidth bw)
{
  _->host_comp.add_ifx(name, bw, 0_ms);
  return *this;
}

Host & Host::remove_ifx(string name)
{
  _->host_comp.remove_ifx(name);
  return *this;
}

unordered_map<string, Interface> & Host::interfaces() const
{
  return _->host_comp.interfaces();
}

vector<Computer> & Host::machines() const 
{ 
  return _->machines; 
}

void Host::removeMachine(string cifx_mac)
{
  _->machines.erase(
    remove_if(_->machines.begin(), _->machines.end(),
      [cifx_mac](const Computer & c)
      {
        return c.interfaces().at("cifx").mac() == cifx_mac;
      }
    ),
    _->machines.end()
  );
}

Host Host::fromJson(Json j)
{
  string name = extract(j, "name", "host");
  Host h{name};
  
  h.cores(extract(j, "cores", "host"))
   .memory(Memory::fromJson(extract(j,"memory", "host")))
   .disk(Memory::fromJson(extract(j, "disk", "host")));

  Json ifxs = extract(j, "interfaces", "host");
  for(const Json & ij : ifxs)
  {
    Interface ifx = Interface::fromJson(ij);
    h.interfaces().insert_or_assign(ifx.name(), ifx);
  }

  Json xpns = extract(j, "machines", "host");
  for(const Json & xj : xpns)
  {
    h.machines().push_back(Computer::fromJson(xj));
  }

  return h;
}

Json Host::json() const
{
  Json j;
  j["name"] = name();
  j["cores"] = cores();
  j["memory"] = memory().json();
  j["disk"] = disk().json();
  j["machines"] = jtransform(_->machines);
  j["interfaces"] = jtransform(interfaces());
  return j;
}

Host Host::clone() const
{
  Host h{name()};
  h._->host_comp = _->host_comp.clone();
  for(auto & c : _->machines)
  { 
    h._->machines.push_back(c.clone()); 
  }
  return h;
}

