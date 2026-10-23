// Signal Companion - Tests for the suite through pldmgr and the Signal suite catalogue (decision log
// D22, D23). SPDX-License-Identifier: GPL-3.0-or-later

#include "pldmgr/suite.hpp"
#include "signal/suite.hpp"
#include "unit/pldmgr_fake.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{

using namespace hui::signal;
using hui::pldmgr::test::FakePldmgr;

// ps5-homeassistant on 9160: its /status says the version that runs. A quit takes it down for one
// /status, as the restart's wait sees it, then the started copy answers. Nothing else answers.
struct Ports
{
    std::mutex mutex;
    int down = 0; // /status calls that still get no answer
    std::string version = "0.2.1";
    std::string next_version = "0.3.0";
    int pushes = 0;
};

class PortLink final : public Transport
{
  public:
    PortLink(int port, Ports &ports) : port_(port), ports_(ports)
    {
    }
    Reply send(std::string_view method, std::string_view path, std::string_view) override
    {
        std::lock_guard<std::mutex> lock(ports_.mutex);
        if (port_ != 9160)
            return {};
        if (method == "POST" && path == "/quit")
        {
            ports_.down = 1;
            return {200, "{}"};
        }
        if (path == "/config")
            return {200, R"({"host":"h","port":1883,"interval":10,"power_actions":false})"};
        if (ports_.down > 0)
        {
            ports_.down--;
            return {};
        }
        return {200, R"({"service":"ps5-homeassistant","version":")" + ports_.version +
                         R"(","state":"connected","error":""})"};
    }
    Reply push(int, std::string_view) override
    {
        std::lock_guard<std::mutex> lock(ports_.mutex);
        ports_.pushes++;
        return {};
    }

  private:
    int port_;
    Ports &ports_;
};

std::string suite_repository()
{
    return std::string(
               R"({"sources":[{"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","error":false,"payloads":[]},)") +
           R"({"id":"s1","name":"Signal suite","url":")" + hui::pldmgr::kSuiteUrl +
           R"(","error":false,"payloads":[)" +
           R"({"id":"ps5-exporter","name":"ps5-exporter","filename":"ps5-exporter.elf","version":"v0.3.1","checksum":"aa"},)" +
           R"({"id":"ps5-homeassistant","name":"ps5-homeassistant","filename":"ps5-homeassistant.elf","version":"v0.3.0","checksum":"bb"},)" +
           R"({"id":"ps5-signal","name":"ps5-signal","filename":"ps5-signal.elf","version":"v0.2.1","checksum":"cc"}]}]})";
}

std::string installed_homeassistant(const std::string &version)
{
    return std::string(R"({"self":{"id":"pldmgr","running_version":"0.6.3"},"items":[)") +
           R"({"slot":"repo/ps5-homeassistant@5u1t3000","path":"/data/pldmgr/payloads/repo/ps5-homeassistant@5u1t3000/ps5-homeassistant.elf",)" +
           R"("kind":"repo","filename":"ps5-homeassistant.elf","id":"ps5-homeassistant","source_url":")" +
           hui::pldmgr::kSuiteUrl + R"(","name":"ps5-homeassistant","version":")" + version +
           R"(","checksum":""}],"usb":[]})";
}

class SuiteCatalogueTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        fake_.reply("GET /version", 200, "0.6.3");
        fake_.reply("GET /sources_list", 200,
                    std::string(R"({"sources":[{"id":"s1","name":"Signal suite","url":")") +
                        hui::pldmgr::kSuiteUrl + R"(","removable":true}]})");
        fake_.reply("GET /get_config", 200,
                    R"({"AUTOLOAD_ENABLED":false,"AUTOLOAD_LIST":"","MULTI_SOURCES_ENABLED":true})");
        fake_.reply("GET /repository_payloads", 200, suite_repository());
        fake_.reply("GET /list_payloads", 200, installed_homeassistant("v0.2.1"));
        fake_.reply("POST /repository_install", 200,
                    R"({"ok":true,"slot":"repo/ps5-homeassistant@n3wc0py0"})");
        fake_.reply("POST /loadpayload:", 200, "OK");
    }
    void TearDown() override
    {
        install_suite([](int) { return std::unique_ptr<Transport>(); });
    }

    // The suite with pldmgr (or without: pldmgr false), polled once.
    Suite &make(bool pldmgr = true)
    {
        Ports *ports = &ports_;
        FakePldmgr *fake = &fake_;
        install_suite([ports](int port) { return std::make_unique<PortLink>(port, *ports); },
                      pldmgr ? Suite::PldmgrFactory([fake] { return fake->transport(); })
                             : Suite::PldmgrFactory());
        Suite &s = suite();
        s.refresh();
        s.wait_idle();
        s.update(0.0f);
        return s;
    }

    // Starts ps5-homeassistant again, as the screen's Update does, and waits for the outcome.
    const PayloadState &update_homeassistant(Suite &s)
    {
        {
            // pldmgr's next answers: what it has after the install, and the new copy runs.
            std::lock_guard<std::mutex> lock(ports_.mutex);
            ports_.version = ports_.next_version;
        }
        s.restart(Payload::homeassistant);
        s.wait_idle();
        s.update(0.0f);
        return s.state(Payload::homeassistant);
    }

    FakePldmgr fake_;
    Ports ports_;
};

TEST_F(SuiteCatalogueTest, ReadsTheCatalogueThroughPldmgr)
{
    Suite &s = make();
    EXPECT_TRUE(s.catalogue().checked);
    EXPECT_TRUE(s.catalogue().pldmgr);
    EXPECT_TRUE(s.catalogue().listed);
    const PayloadState &ha = s.state(Payload::homeassistant);
    EXPECT_EQ(ha.version, "0.2.1");
    EXPECT_EQ(ha.catalogue_version, "0.3.0");
    EXPECT_EQ(ha.pldmgr_version, "0.2.1");
    EXPECT_EQ(s.state(Payload::exporter).catalogue_version, "0.3.1");
    EXPECT_EQ(s.state(Payload::exporter).pldmgr_version, "");
    // The catalogue was there and more sources were on: nothing changed in pldmgr.
    EXPECT_FALSE(fake_.last("POST", "/sources_add").has_value());
    EXPECT_FALSE(fake_.last("POST", "/set_config").has_value());
}

TEST_F(SuiteCatalogueTest, AddsTheCatalogueToPldmgrWhenMissing)
{
    fake_.reply("GET /sources_list", 200, R"({"sources":[]})");
    fake_.reply("GET /get_config", 200,
                R"({"AUTOLOAD_ENABLED":false,"AUTOLOAD_LIST":"","MULTI_SOURCES_ENABLED":false})");
    fake_.reply("POST /sources_add", 200, R"({"ok":true,"name":"Signal suite"})");
    fake_.reply("POST /set_config", 200, "OK");
    make();
    const auto added = fake_.last("POST", "/sources_add");
    ASSERT_TRUE(added.has_value());
    EXPECT_NE(added->target.find("ps5-signal-suite"), std::string::npos);
    EXPECT_EQ(fake_.last("POST", "/set_config")->body, R"({"MULTI_SOURCES_ENABLED":true})");
}

TEST_F(SuiteCatalogueTest, UpdatesFromTheCatalogueAndStartsPldmgrsCopy)
{
    Suite &s = make();
    const PayloadState &ha = update_homeassistant(s);
    const auto install = fake_.last("POST", "/repository_install");
    ASSERT_TRUE(install.has_value());
    EXPECT_NE(install->target.find("id=ps5-homeassistant"), std::string::npos);
    const auto launch = fake_.last("POST", "/loadpayload:");
    ASSERT_TRUE(launch.has_value());
    EXPECT_NE(launch->target.find("ps5-homeassistant@n3wc0py0"), std::string::npos);
    EXPECT_EQ(ports_.pushes, 0); // not the app's copy
    EXPECT_FALSE(ha.failed);
    EXPECT_NE(ha.message.find("0.3.0 runs, from the Signal suite catalogue"), std::string::npos);
}

TEST_F(SuiteCatalogueTest, StartsPldmgrsCopyWhenItIsUpToDate)
{
    fake_.reply("GET /list_payloads", 200, installed_homeassistant("v0.3.0"));
    Suite &s = make();
    update_homeassistant(s);
    EXPECT_FALSE(fake_.last("POST", "/repository_install").has_value());
    const auto launch = fake_.last("POST", "/loadpayload:");
    ASSERT_TRUE(launch.has_value());
    EXPECT_NE(launch->target.find("ps5-homeassistant@5u1t3000"), std::string::npos);
}

TEST_F(SuiteCatalogueTest, LeavesAConflictToPldmgr)
{
    fake_.reply("POST /repository_install", 200,
                R"({"ok":false,"conflict":{"slot":"repo/ps5-homeassistant@aa","source_name":"Other","version":"0.1.0"}})");
    fake_.reply("GET /list_payloads", 200, R"({"self":{},"items":[],"usb":[]})");
    Suite &s = make();
    const PayloadState &ha = update_homeassistant(s);
    EXPECT_TRUE(ha.failed);
    EXPECT_NE(ha.message.find("from another repository"), std::string::npos);
    EXPECT_FALSE(fake_.last("POST", "/loadpayload:").has_value());
    EXPECT_EQ(ports_.pushes, 0);
}

TEST_F(SuiteCatalogueTest, UsesTheAppsCopyWithoutPldmgr)
{
    fake_.set_offline(true);
    Suite &s = make();
    EXPECT_TRUE(s.catalogue().checked);
    EXPECT_FALSE(s.catalogue().pldmgr);
    EXPECT_EQ(s.state(Payload::homeassistant).catalogue_version, "");
    const PayloadState &ha = update_homeassistant(s);
    EXPECT_FALSE(fake_.last("POST", "/repository_install").has_value());
    // Without the app's ELF in the test's assets the old path says so; it did try that path.
    EXPECT_TRUE(ha.failed);
    EXPECT_NE(ha.message.find("does not include ps5-homeassistant.elf"), std::string::npos);
}

TEST_F(SuiteCatalogueTest, FallsBackWhenPldmgrCannotInstall)
{
    fake_.reply("POST /repository_install", 400, R"({"ok":false,"message":"Checksum mismatch"})");
    Suite &s = make();
    const PayloadState &ha = update_homeassistant(s);
    EXPECT_FALSE(fake_.last("POST", "/loadpayload:").has_value());
    EXPECT_NE(ha.message.find("does not include ps5-homeassistant.elf"), std::string::npos);
}

TEST(SuiteVersions, DropsTheLeadingV)
{
    EXPECT_EQ(plain_version("v0.3.0"), "0.3.0");
    EXPECT_EQ(plain_version("0.3.0"), "0.3.0");
    EXPECT_EQ(plain_version(""), "");
}

} // namespace
