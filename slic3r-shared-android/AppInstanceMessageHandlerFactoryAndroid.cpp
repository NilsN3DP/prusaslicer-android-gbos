#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"

// Android runs a single instance per app, so there is nobody to talk to: the D-Bus based
// Linux implementation is replaced by no-ops.

namespace Slic3r::Biz::AppInstance {

namespace {

class AppInstanceMessageSenderAndroid : public AbstractAppInstanceMessageSender
{
public:
    void multicast_message(const std::string&, const std::string&, size_t, void*) override {}
    void broadcast_message(const std::string&, const std::string&, size_t, void*) override {}
};

class AppInstanceMessageHandlerAndroid : public AbstractAppInstanceMessageHandler
{
public:
    using AbstractAppInstanceMessageHandler::AbstractAppInstanceMessageHandler;

    void init(void*) override {}
    void multicast_message(const std::string&, const std::string&) override {}
    void on_becoming_primary_instance() override {}
};

} // namespace

std::unique_ptr<AbstractAppInstanceMessageSender> create_app_instance_message_sender()
{
    return std::make_unique<AppInstanceMessageSenderAndroid>();
}

std::unique_ptr<AbstractAppInstanceMessageHandler> create_app_instance_message_handler(
    Platform::IMainThreadDispatcher& dispatcher
)
{
    return std::make_unique<AppInstanceMessageHandlerAndroid>(dispatcher);
}

} // namespace Slic3r::Biz::AppInstance
