#ifndef CROSSPLATUTILS_H
#define CROSSPLATUTILS_H

namespace Vulkanised
{
    #if (defined(__clang__) && defined(__APPLE__)) // || (defined(__GNUC__) && defined(__linux__))    // In case Linux cant do atomics either...
   
    // Atomic seems to be a debated topic among different C++ compilers, but we still wish to have the thread safe pattern.
    // So we just make one, in case we aren't on Windows, using mutex.
    template <typename T>
    class VulkanisedAtomicSharedPtr {
    private:
        std::shared_ptr<T> ptr;
        mutable std::mutex mtx;
    public:
        VulkanisedAtomicSharedPtr() = default;
        VulkanisedAtomicSharedPtr(std::shared_ptr<T> p) : ptr(std::move(p)) {}

        void store(std::shared_ptr<T> desired) {
            std::lock_guard<std::mutex> lock(mtx);
            ptr = std::move(desired);
        }

        std::shared_ptr<T> load() const {
            std::lock_guard<std::mutex> lock(mtx);
            return ptr;
        }

        VulkanisedAtomicSharedPtr& operator=(std::shared_ptr<T> desired) {
            store(std::move(desired));
            return *this;
        }
    };

    // The alias "VAtomic" shall hold this struct.
    template <typename T>
    using VAtomic = VulkanisedAtomicSharedPtr<T>;

    #else
    // If NOT on Mac, then just assign the same alias to the actual standard library implementation.
    template <typename T>
    using VAtomic = std::atomic<std::shared_ptr<T>>;
    #endif
}

#endif  //  !CROSSPLATUTILS_H