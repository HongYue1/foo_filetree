#include "enumeration_service.h"

namespace filetree::fs {

EnumerationService::EnumerationService(Poster poster, unsigned threads)
    : poster_(std::move(poster)), pool_(threads) {}

EnumerationService::Ticket EnumerationService::request(std::wstring folder, EnumOptions options,
                                                       Callback on_done) {
    auto flag = std::make_shared<std::atomic<bool>>(false);
    // The task copies the poster rather than capturing `this`: a worker detached at shutdown may
    // still finish after the service is gone.
    pool_.submit([poster = poster_, flag, folder = std::move(folder), options = std::move(options),
                  on_done = std::move(on_done)]() mutable {
        if (flag->load(std::memory_order_relaxed)) return;
        // std::function needs a copyable callable, hence the shared_ptr around the listing.
        auto listing = std::make_shared<Listing>(enumerate_folder(folder, options, *flag));
        if (listing->cancelled || flag->load(std::memory_order_relaxed)) return;
        poster([flag, listing, on_done = std::move(on_done)] {
            if (!flag->load(std::memory_order_relaxed)) on_done(*listing);
        });
    });
    return Ticket(std::move(flag));
}

} // namespace filetree::fs
