#pragma once

#include <cstddef>
#include <mutex>
#include <string>

namespace ninjamplugin::core {

struct ServerLicenseAgreement {
  enum class State {
    kNone = 0,
    kPending,
    kOversized,
  };

  State state = State::kNone;
  std::string server;
  std::string text;

  bool RequiresDecision() const { return state != State::kNone; }
  bool CanAccept() const { return state == State::kPending; }
  bool operator==(const ServerLicenseAgreement&) const = default;
};

// Owns the non-persistent consent associated with one plug-in instance.
// Agreement text is untrusted network input: it is copied with a strict bound
// and is never interpreted as markup or written to saved state.
class ServerLicenseConsent final {
 public:
  static constexpr std::size_t kMaximumAgreementBytes = 64U * 1024U;

  // Called synchronously by the NINJAM authentication callback. Returns true
  // only when this exact server/text pair was explicitly accepted earlier.
  bool Observe(const std::string& server, const char* agreement_text);

  ServerLicenseAgreement GetPendingAgreement() const;
  bool AcceptPendingAgreement();
  void DeclinePendingAgreement();
  void DismissPendingAgreement();
  void ClearForServerChange();

 private:
  mutable std::mutex mutex_;
  ServerLicenseAgreement pending_;
  std::string accepted_server_;
  std::string accepted_text_;
};

}  // namespace ninjamplugin::core
