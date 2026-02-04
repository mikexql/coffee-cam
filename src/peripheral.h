#ifndef PERIPHERAL_H
#define PERIPHERAL_H

class Peripheral
{
public:
    explicit Peripheral(const char *name) : name_(name), initialised_(false) {}
    virtual ~Peripheral() = default;

    const char *getName() const { return name_; }
    bool isInitialized() const { return initialised_; }

    // Non-virtual: standardizes the init flow
    bool initialize()
    {
        if (initialised_)
        {
            return true;
        }
        const bool ok = begin();
        initialised_ = ok;
        return ok;
    }

    // Device-specific init
    virtual bool begin() = 0;

    // Optional lifecycle hooks (default no-op success)
    virtual bool wake() { return true; }
    virtual bool sleep() { return true; }

protected:
    void setInitialized(bool value) { initialised_ = value; }

private:
    const char *name_;
    bool initialised_;
};

#endif // PERIPHERAL_H