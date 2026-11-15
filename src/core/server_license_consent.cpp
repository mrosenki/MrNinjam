#include "ninjamplugin/core/server_license_consent.h"

#include <string_view>

namespace ninjamplugin::core {
namespace {

std::size_t BoundedLength(const char* text, const std::size_t maximum) {
  if (text == nullptr) return 0;
  std::size_t length = 0;
  while (length <= maximum && text[length] != '\0') ++length;
  return length;
}

}  // namespace

bool ServerLicenseConsent::Observe(const std::string& server,
                                   const char* agreement_text) {
  const std::size_t length = BoundedLength(agreement_text, kMaximumAgreementBytes);
  if (length == 0) return true;

  std::lock_guard<std::mutex> lock(mutex_);
  if (length <= kMaximumAgreementBytes) {
    const std::string_view agreement(agreement_text, length);
    if (accepted_server_ == server && accepted_text_ == agreement) return true;

    pending_.state = ServerLicenseAgreement::State::kPending;
    pending_.server = server;
    pending_.text.assign(agreement);
  } else {
    pending_.state = ServerLicenseAgreement::State::kOversized;
    pending_.server = server;
    pending_.text.clear();
  }
  return false;
}

ServerLicenseAgreement ServerLicenseConsent::GetPendingAgreement() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_;
}

bool ServerLicenseConsent::AcceptPendingAgreement() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_.CanAccept()) return false;
  accepted_server_ = pending_.server;
  accepted_text_ = pending_.text;
  pending_ = {};
  return true;
}

void ServerLicenseConsent::DeclinePendingAgreement() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_ = {};
}

void ServerLicenseConsent::DismissPendingAgreement() {
  DeclinePendingAgreement();
}

void ServerLicenseConsent::ClearForServerChange() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_ = {};
  accepted_server_.clear();
  accepted_text_.clear();
}

}  // namespace ninjamplugin::core
