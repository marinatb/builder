#include "topologies.hxx"
#include "3p/pipes/pipes.hxx"

using std::string;
using std::to_string;
using namespace marina;
using namespace pipes;
using namespace std::literals::string_literals;

TestbedTopology marina::minibed()
{
  TestbedTopology t{"minibed"};

  auto sw = t.sw("main-switch").backplane(1440_gbps);

  range(0,2)
    | map([](int x) { return to_string(x); })
    | for_each([&sw, &t](string x) 
      {
        auto c = 
        t.host("mrtb"+x)
          .cores(24)
          .memory(24_gb)
          .disk(240_gb)
          //TODO should be ens2f0 with properties that tell marina that
          //this interface can use dpdk so host-control automatically
          //sets it up
          .add_ifx("dpdk0", 10_gbps);

        t.connect({c, c.ifx("dpdk0")}, sw, 24_gbps);
     });

  return t;
}
