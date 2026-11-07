#include "GatherHandler.hpp"
#include "ScatterHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/InspectionHeader.hpp"
#include "backend/message/InspectionView.hpp"
#include "backend/message/MessageView.hpp"
#include "core/config/Configuration.hpp"
#include "core/net/UdpMetadata.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

class MockRecordingChannel : public ::Backend::IOutputChannel {
public:
    explicit MockRecordingChannel(std::string name)
        : ::Backend::IOutputChannel(std::move(name))
    {
    }

    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        rawEnvelopes.emplace_back(message.begin(), message.end());
        return true;
    }

    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override
    {
        for (const auto& msg : messages) {
            send(msg);
        }
        return messages.size();
    }

    std::mutex m_mutex;
    std::vector<std::vector<std::byte>> rawEnvelopes;
};

} // namespace

TEST_CASE("Scatter-Gather Pipeline: Fork-Join Inspection Flow", "[handler][scatter_gather]")
{
    Core::Simulation::SimulationBackend backend;

    SECTION("Scatter preserves untouched protocol and tunnels original metadata")
    {
        Proxy::Handler::ScatterHandler scatter { backend };

        auto outChan0 = std::make_unique<MockRecordingChannel>("insp_branch_0");
        auto outChan1 = std::make_unique<MockRecordingChannel>("insp_branch_1");
        auto outChan2 = std::make_unique<MockRecordingChannel>("insp_branch_2");

        auto* rawPtr0 = outChan0.get();
        auto* rawPtr1 = outChan1.get();
        auto* rawPtr2 = outChan2.get();

        auto ptr0 = scatter.registerOutputChannel(std::move(outChan0));
        auto ptr1 = scatter.registerOutputChannel(std::move(outChan1));
        auto ptr2 = scatter.registerOutputChannel(std::move(outChan2));

        scatter.addOutputChannel(ptr0);
        scatter.addOutputChannel(ptr1);
        scatter.addOutputChannel(ptr2);

        // Pack an incoming packet with UdpMetadata (protocol = IPPROTO_UDP = 17)
        Core::Net::UdpMetadata udpMeta;
        udpMeta.srcIp = inet_addr("192.168.1.100");
        udpMeta.dstIp = inet_addr("10.0.0.1");
        udpMeta.srcPort = htons(9050);
        udpMeta.dstPort = htons(8080);
        udpMeta.addressFamily = 4;

        std::string payloadStr = "{\"action\":\"inspect_me\",\"packetId\":42}";
        auto udpEnvelope = Backend::MessageView::pack(
            udpMeta,
            Common::Compat::span<const std::byte>(reinterpret_cast<const std::byte*>(payloadStr.data()), payloadStr.size()))
                               .value();
        auto optMsg = Backend::MessageView::from(Common::Compat::span<const std::byte>(udpEnvelope.data(), udpEnvelope.size()));
        REQUIRE(optMsg.has_value());
        REQUIRE(optMsg->protocol().value() == IPPROTO_UDP);

        auto scatterRes = scatter.onMessageReceived("raw_in", *optMsg);
        REQUIRE(scatterRes.is<Backend::Forwarded>());

        REQUIRE(rawPtr0->rawEnvelopes.size() == 1);
        REQUIRE(rawPtr1->rawEnvelopes.size() == 1);
        REQUIRE(rawPtr2->rawEnvelopes.size() == 1);

        auto view0 = Backend::MessageView::from(Common::Compat::span<const std::byte>(rawPtr0->rawEnvelopes[0].data(), rawPtr0->rawEnvelopes[0].size()));
        REQUIRE(view0.has_value());
        // Verify protocol is still untouched IPPROTO_UDP!
        CHECK(view0->protocol().value() == IPPROTO_UDP);

        auto inspView0 = Backend::InspectionView::from(*view0);
        REQUIRE(inspView0.has_value());
        auto header0 = inspView0->extractHeader();
        CHECK(header0.correlationId > 0);
        CHECK(header0.totalSplits == 3);
        CHECK(header0.splitIndex == 0);
        CHECK(header0.verdict == Backend::InspectionVerdict::Pending);
        CHECK(header0.originalMetadataLength == sizeof(Core::Net::UdpMetadata));

        // Verify inspector can access original tunneled UdpMetadata
        auto origMetaSpan = inspView0->extractMetadata();
        REQUIRE(origMetaSpan.size() == sizeof(Core::Net::UdpMetadata));
        auto extractedUdp = inspView0->extractMetadataAs<Core::Net::UdpMetadata>();
        REQUIRE(extractedUdp.has_value());
        CHECK(ntohs(extractedUdp->srcPort) == 9050);
        CHECK(ntohs(extractedUdp->dstPort) == 8080);

        auto view2 = Backend::MessageView::from(Common::Compat::span<const std::byte>(rawPtr2->rawEnvelopes[0].data(), rawPtr2->rawEnvelopes[0].size()));
        REQUIRE(view2.has_value());
        auto inspView2 = Backend::InspectionView::from(*view2);
        REQUIRE(inspView2.has_value());
        auto header2 = inspView2->extractHeader();
        CHECK(header2.correlationId == header0.correlationId);
        CHECK(header2.splitIndex == 2);
    }

    SECTION("Gather restores exact original protocol and UdpMetadata to egress")
    {
        Proxy::Handler::GatherHandler gather { backend };

        auto egressChannel = std::make_unique<MockRecordingChannel>("egress_out");
        auto* egressPtr = egressChannel.get();
        auto egressReg = gather.registerOutputChannel(std::move(egressChannel));
        gather.setOutputChannel(egressReg);

        std::string payloadStr = "SECURE_PAYLOAD_DATA";
        const auto payloadSpan = Common::Compat::span<const std::byte>(
            reinterpret_cast<const std::byte*>(payloadStr.data()), payloadStr.size());

        Core::Net::UdpMetadata origUdp;
        origUdp.srcIp = inet_addr("172.16.0.5");
        origUdp.dstIp = inet_addr("10.10.10.1");
        origUdp.srcPort = htons(54321);
        origUdp.dstPort = htons(443);

        auto innerEnv = Backend::MessageView::pack(
            origUdp,
            payloadSpan)
                            .value();
        auto innerMsg = Backend::MessageView::from(Common::Compat::span<const std::byte>(innerEnv.data(), innerEnv.size()));
        REQUIRE(innerMsg.has_value());

        Backend::InspectionHeader header0 {
            .magic = Backend::PipelineInspectionMagic,
            .correlationId = 1001,
            .totalSplits = 2,
            .splitIndex = 0,
            .verdict = Backend::InspectionVerdict::Pass,
            .errorCode = 0,
            .originalMetadataLength = sizeof(origUdp)
        };

        auto env0 = Backend::InspectionView::pack(*innerMsg, header0).value();
        auto msg0 = Backend::MessageView::from(Common::Compat::span<const std::byte>(env0.data(), env0.size()));
        REQUIRE(msg0.has_value());

        // Process first split -> consumed
        auto res0 = gather.onMessageReceived("res_branch_0", *msg0);
        CHECK(res0.is<Backend::Consumed>());
        CHECK(egressPtr->rawEnvelopes.empty());

        Backend::InspectionHeader header1 = header0;
        header1.splitIndex = 1;
        auto env1 = Backend::InspectionView::pack(*innerMsg, header1).value();
        auto msg1 = Backend::MessageView::from(Common::Compat::span<const std::byte>(env1.data(), env1.size()));
        REQUIRE(msg1.has_value());

        // Process second split -> all splits arrived, should forward!
        auto res1 = gather.onMessageReceived("res_branch_1", *msg1);
        CHECK(res1.is<Backend::Forwarded>());

        REQUIRE(egressPtr->rawEnvelopes.size() == 1);
        auto egressView = Backend::MessageView::from(Common::Compat::span<const std::byte>(egressPtr->rawEnvelopes[0].data(), egressPtr->rawEnvelopes[0].size()));
        REQUIRE(egressView.has_value());

        // Critical check: protocol and UdpMetadata are restored!
        CHECK(egressView->protocol().value() == IPPROTO_UDP);
        auto restoredUdp = egressView->getMetadata<Core::Net::UdpMetadata>();
        REQUIRE(restoredUdp.has_value());
        CHECK(ntohs(restoredUdp->srcPort) == 54321);
        CHECK(ntohs(restoredUdp->dstPort) == 443);

        std::string reconstructed(reinterpret_cast<const char*>(egressView->payload().data()), egressView->payload().size());
        CHECK(reconstructed == payloadStr);
    }

    SECTION("Gather drops immediately on rejection verdict")
    {
        Proxy::Handler::GatherHandler gather { backend };

        auto egressChannel = std::make_unique<MockRecordingChannel>("egress_out");
        auto* egressPtr = egressChannel.get();
        auto egressReg = gather.registerOutputChannel(std::move(egressChannel));
        gather.setOutputChannel(egressReg);

        std::string payloadStr = "MALICIOUS_PAYLOAD";
        const auto payloadSpan = Common::Compat::span<const std::byte>(
            reinterpret_cast<const std::byte*>(payloadStr.data()), payloadStr.size());

        auto rawMsg = Backend::MessageView(payloadSpan);

        Backend::InspectionHeader rejectHeader {
            .magic = Backend::PipelineInspectionMagic,
            .correlationId = 2002,
            .totalSplits = 2,
            .splitIndex = 0,
            .verdict = Backend::InspectionVerdict::Reject,
            .errorCode = 403,
            .originalMetadataLength = 0
        };

        auto envReject = Backend::InspectionView::pack(rawMsg, rejectHeader).value();
        auto msgReject = Backend::MessageView::from(Common::Compat::span<const std::byte>(envReject.data(), envReject.size()));
        REQUIRE(msgReject.has_value());

        auto res = gather.onMessageReceived("res_branch_0", *msgReject);
        REQUIRE(std::holds_alternative<Backend::Dropped>(res));
        auto dropped = std::get<Backend::Dropped>(res);
        CHECK(dropped.errorCode == 403);
        CHECK(egressPtr->rawEnvelopes.empty());
    }

    SECTION("Gather evicts expired entries on timeout")
    {
        Proxy::Handler::GatherHandler gather { backend };

        Core::Config::Configuration gatherConfig;
        gatherConfig.set("gather.input_channels", "res_branch_0,res_branch_1");
        gatherConfig.set("gather.output_channel", "egress_out");
        gatherConfig.set("gather.timeout_ms", "50"); // fast timeout for test
        REQUIRE(gather.configure(gatherConfig));

        std::string payloadStr = "UNFINISHED_PAYLOAD";
        const auto payloadSpan = Common::Compat::span<const std::byte>(
            reinterpret_cast<const std::byte*>(payloadStr.data()), payloadStr.size());

        auto rawMsg = Backend::MessageView(payloadSpan);

        Backend::InspectionHeader header0 {
            .magic = Backend::PipelineInspectionMagic,
            .correlationId = 3003,
            .totalSplits = 2,
            .splitIndex = 0,
            .verdict = Backend::InspectionVerdict::Pass,
            .errorCode = 0,
            .originalMetadataLength = 0
        };

        auto env0 = Backend::InspectionView::pack(rawMsg, header0).value();
        auto msg0 = Backend::MessageView::from(Common::Compat::span<const std::byte>(env0.data(), env0.size()));
        REQUIRE(msg0.has_value());

        gather.onMessageReceived("res_branch_0", *msg0);

        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        gather.step(); // Triggers eviction
    }
}
