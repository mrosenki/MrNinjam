#include "ninjamplugin/core/server_license_consent.h"

#include <iostream>
#include <string>

int main() {
  using ninjamplugin::core::ServerLicenseAgreement;
  using ninjamplugin::core::ServerLicenseConsent;

  ServerLicenseConsent consent;
  if (!consent.Observe("server.test:2049", nullptr) ||
      !consent.Observe("server.test:2049", "")) {
    std::cerr << "Empty agreements must not require consent\n";
    return 1;
  }

  if (consent.Observe("server.test:2049", "Synthetic terms v1")) {
    std::cerr << "New agreement was accepted without user consent\n";
    return 1;
  }
  auto pending = consent.GetPendingAgreement();
  if (pending.state != ServerLicenseAgreement::State::kPending ||
      pending.server != "server.test:2049" || pending.text != "Synthetic terms v1") {
    std::cerr << "Pending agreement was not copied exactly\n";
    return 1;
  }
  if (!consent.AcceptPendingAgreement() ||
      !consent.Observe("server.test:2049", "Synthetic terms v1")) {
    std::cerr << "Explicitly accepted agreement was not recognized\n";
    return 1;
  }

  if (consent.Observe("server.test:2049", "Synthetic terms v2")) {
    std::cerr << "Changed agreement reused stale consent\n";
    return 1;
  }
  consent.DeclinePendingAgreement();
  if (consent.GetPendingAgreement().RequiresDecision()) {
    std::cerr << "Decline did not clear the pending decision\n";
    return 1;
  }
  if (consent.Observe("server.test:2049", "Synthetic terms v2")) {
    std::cerr << "Declined agreement was accepted on a later connection\n";
    return 1;
  }
  pending = consent.GetPendingAgreement();
  if (!pending.RequiresDecision() || pending.text != "Synthetic terms v2") {
    std::cerr << "A later connection did not present declined terms again\n";
    return 1;
  }
  consent.DismissPendingAgreement();

  ServerLicenseConsent other_instance;
  if (other_instance.Observe("server.test:2049", "Synthetic terms v1") ||
      !other_instance.GetPendingAgreement().RequiresDecision()) {
    std::cerr << "Consent leaked between plug-in instances\n";
    return 1;
  }
  other_instance.DismissPendingAgreement();

  if (consent.Observe("other.test:2049", "Synthetic terms v1")) {
    std::cerr << "Consent leaked to a different server\n";
    return 1;
  }
  consent.DismissPendingAgreement();

  std::string oversized(ServerLicenseConsent::kMaximumAgreementBytes + 1U, 'x');
  if (consent.Observe("server.test:2049", oversized.c_str())) {
    std::cerr << "Oversized agreement was accepted\n";
    return 1;
  }
  pending = consent.GetPendingAgreement();
  if (pending.state != ServerLicenseAgreement::State::kOversized ||
      !pending.text.empty() || consent.AcceptPendingAgreement()) {
    std::cerr << "Oversized agreement was retained or made acceptable\n";
    return 1;
  }

  consent.ClearForServerChange();
  if (consent.GetPendingAgreement().RequiresDecision() ||
      consent.Observe("server.test:2049", "Synthetic terms v1")) {
    std::cerr << "Server change did not revoke in-memory consent\n";
    return 1;
  }
  return 0;
}
