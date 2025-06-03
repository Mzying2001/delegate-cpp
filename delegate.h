#ifndef _DELEGATE_H_
#define _DELEGATE_H_

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <type_traits>
#include <vector>

template <typename>
struct ICallable;

template <typename>
class Delegate;

template <typename TRet, typename... Args>
struct ICallable<TRet(Args...)> {
    virtual ~ICallable()                              = default;
    virtual TRet Invoke(Args... args) const           = 0;
    virtual ICallable *Clone() const                  = 0;
    virtual const std::type_info &GetTypeInfo() const = 0;
    virtual bool Equals(const ICallable &other) const = 0;
};

template <typename TRet, typename... Args>
class Delegate<TRet(Args...)> final : public ICallable<TRet(Args...)>
{
private:
    using _ICallable = ICallable<TRet(Args...)>;

    template <typename T, typename = void>
    struct _IsEqualityComparable : std::false_type {
    };

    template <typename T>
    struct _IsEqualityComparable<
        T,
        typename std::enable_if<true, decltype(void(std::declval<T>() == std::declval<T>()))>::type> : std::true_type {
    };

    template <typename T, typename = void>
    struct _IsMemcmpSafe : std::false_type {
    };

    template <typename T>
    struct _IsMemcmpSafe<
        T,
        typename std::enable_if</*std::is_trivial<T>::value &&*/ std::is_standard_layout<T>::value, void>::type> : std::true_type {
    };

    template <typename T>
    class _CallableWrapperImpl : public _ICallable
    {
        alignas(T) mutable uint8_t _storage[sizeof(T)];

    public:
        _CallableWrapperImpl(const T &value)
        {
            memset(_storage, 0, sizeof(_storage));
            new (_storage) T(value);
        }
        _CallableWrapperImpl(T &&value)
        {
            memset(_storage, 0, sizeof(_storage));
            new (_storage) T(std::move(value));
        }
        virtual ~_CallableWrapperImpl()
        {
            GetValue().~T();
            // memset(_storage, 0, sizeof(_storage));
        }
        T &GetValue() const noexcept
        {
            return *reinterpret_cast<T *>(_storage);
        }
        TRet Invoke(Args... args) const override
        {
            return GetValue()(std::forward<Args>(args)...);
        }
        _ICallable *Clone() const override
        {
            return new _CallableWrapperImpl(GetValue());
        }
        const std::type_info &GetTypeInfo() const override
        {
            return typeid(T);
        }
        bool Equals(const _ICallable &other) const override
        {
            return EqualsImpl(other);
        }
        template <typename U = T>
        typename std::enable_if<_IsEqualityComparable<U>::value, bool>::type
        EqualsImpl(const _ICallable &other) const
        {
            if (this == &other) {
                return true;
            }
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            const auto &otherWrapper = static_cast<const _CallableWrapperImpl &>(other);
            return GetValue() == otherWrapper.GetValue();
        }
        template <typename U = T>
        typename std::enable_if<!_IsEqualityComparable<U>::value && _IsMemcmpSafe<U>::value, bool>::type
        EqualsImpl(const _ICallable &other) const
        {
            if (this == &other) {
                return true;
            }
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            const auto &otherWrapper = static_cast<const _CallableWrapperImpl &>(other);
            return memcmp(_storage, otherWrapper._storage, sizeof(_storage)) == 0;
        }
        template <typename U = T>
        typename std::enable_if<!_IsEqualityComparable<U>::value && !_IsMemcmpSafe<U>::value, bool>::type
        EqualsImpl(const _ICallable &other) const
        {
            return this == &other;
        }
    };

    template <typename T>
    using _CallableWrapper = _CallableWrapperImpl<typename std::decay<T>::type>;

    template <typename T>
    class _MemberFuncWrapper : public _ICallable
    {
        T *obj;
        TRet (T::*func)(Args...);

    public:
        _MemberFuncWrapper(T &obj, TRet (T::*func)(Args...)) : obj(&obj), func(func)
        {
        }
        TRet Invoke(Args... args) const override
        {
            return (obj->*func)(std::forward<Args>(args)...);
        }
        _ICallable *Clone() const override
        {
            return new _MemberFuncWrapper(*obj, func);
        }
        const std::type_info &GetTypeInfo() const override
        {
            return typeid(func);
        }
        bool Equals(const _ICallable &other) const override
        {
            if (this == &other) {
                return true;
            }
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            const auto &otherWrapper = static_cast<const _MemberFuncWrapper &>(other);
            return obj == otherWrapper.obj && func == otherWrapper.func;
        }
    };

    template <typename T>
    class _ConstMemberFuncWrapper : public _ICallable
    {
        const T *obj;
        TRet (T::*func)(Args...) const;

    public:
        _ConstMemberFuncWrapper(const T &obj, TRet (T::*func)(Args...) const) : obj(&obj), func(func)
        {
        }
        TRet Invoke(Args... args) const override
        {
            return (obj->*func)(std::forward<Args>(args)...);
        }
        _ICallable *Clone() const override
        {
            return new _ConstMemberFuncWrapper(*obj, func);
        }
        const std::type_info &GetTypeInfo() const override
        {
            return typeid(func);
        }
        bool Equals(const _ICallable &other) const override
        {
            if (this == &other) {
                return true;
            }
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            const auto &otherWrapper = static_cast<const _ConstMemberFuncWrapper &>(other);
            return obj == otherWrapper.obj && func == otherWrapper.func;
        }
    };

private:
    std::vector<std::unique_ptr<_ICallable>> _data;

public:
    Delegate() = default;

    Delegate(const ICallable<TRet(Args...)> &callable)
    {
        Add(callable);
    }

    Delegate(TRet (*func)(Args...))
    {
        Add(func);
    }

    template <typename T>
    Delegate(const T &callable)
    {
        Add(callable);
    }

    Delegate(const Delegate &other)
    {
        _data.reserve(other._data.size());
        for (const auto &item : other._data) {
            _data.emplace_back(item->Clone());
        }
    }

    Delegate(Delegate &&other) : _data(std::move(other._data))
    {
    }

    Delegate &operator=(const Delegate &other)
    {
        if (this != &other) {
            _data.clear();
            _data.reserve(other._data.size());
            for (const auto &item : other._data) {
                _data.emplace_back(item->Clone());
            }
        }
        return *this;
    }

    Delegate &operator=(Delegate &&other)
    {
        if (this != &other) {
            _data = std::move(other._data);
        }
        return *this;
    }

    void Add(const ICallable<TRet(Args...)> &callable)
    {
        _data.emplace_back(callable.Clone());
    }

    void Add(TRet (*func)(Args...))
    {
        if (func != nullptr) {
            _data.emplace_back(std::make_unique<_CallableWrapper<decltype(func)>>(func));
        }
    }

    template <typename T>
    void Add(const T &callable)
    {
        _data.emplace_back(std::make_unique<_CallableWrapper<T>>(callable));
    }

    template <typename T>
    void Add(T &obj, TRet (T::*func)(Args...))
    {
        _data.emplace_back(std::make_unique<_MemberFuncWrapper<T>>(obj, func));
    }

    template <typename T>
    void Add(const T &obj, TRet (T::*func)(Args...) const)
    {
        _data.emplace_back(std::make_unique<_ConstMemberFuncWrapper<T>>(obj, func));
    }

    void Clear()
    {
        _data.clear();
    }

    bool Remove(const ICallable<TRet(Args...)> &callable)
    {
        return _Remove(callable);
    }

    bool Remove(TRet (*func)(Args...))
    {
        if (func == nullptr) {
            return false;
        }
        return _Remove(_CallableWrapper<decltype(func)>(func));
    }

    template <typename T>
    bool Remove(const T &callable)
    {
        return _Remove(_CallableWrapper<T>(callable));
    }

    template <typename T>
    bool Remove(T &obj, TRet (T::*func)(Args...))
    {
        return _Remove(_MemberFuncWrapper<T>(obj, func));
    }

    template <typename T>
    bool Remove(const T &obj, TRet (T::*func)(Args...) const)
    {
        return _Remove(_ConstMemberFuncWrapper<T>(obj, func));
    }

    TRet operator()(Args... args) const
    {
        return Invoke(std::forward<Args>(args)...);
    }

    bool operator==(const Delegate &other) const
    {
        return Equals(other);
    }

    bool operator!=(const Delegate &other) const
    {
        return !Equals(other);
    }

    bool operator==(std::nullptr_t) const
    {
        return _data.empty();
    }

    bool operator!=(std::nullptr_t) const
    {
        return !_data.empty();
    }

    operator bool() const
    {
        return !_data.empty();
    }

    Delegate &operator+=(const ICallable<TRet(Args...)> &callable)
    {
        Add(callable);
        return *this;
    }

    Delegate &operator+=(TRet (*func)(Args...))
    {
        Add(func);
        return *this;
    }

    template <typename T>
    Delegate &operator+=(const T &callable)
    {
        Add(callable);
        return *this;
    }

    Delegate &operator-=(const ICallable<TRet(Args...)> &callable)
    {
        Remove(callable);
        return *this;
    }

    Delegate &operator-=(TRet (*func)(Args...))
    {
        Remove(func);
        return *this;
    }

    template <typename T>
    Delegate &operator-=(const T &callable)
    {
        Remove(callable);
        return *this;
    }

    virtual TRet Invoke(Args... args) const override
    {
        if (_data.empty()) {
            throw std::runtime_error("Delegate is empty");
        }
        for (size_t i = 0; i < _data.size() - 1; ++i) {
            _data[i]->Invoke(std::forward<Args>(args)...);
        }
        return _data.back()->Invoke(std::forward<Args>(args)...);
    }

    virtual ICallable<TRet(Args...)> *Clone() const override
    {
        return new Delegate(*this);
    }

    virtual const std::type_info &GetTypeInfo() const override
    {
        return typeid(Delegate<TRet(Args...)>);
    }

    virtual bool Equals(const ICallable<TRet(Args...)> &other) const override
    {
        if (this == &other) {
            return true;
        }
        if (GetTypeInfo() != other.GetTypeInfo()) {
            return false;
        }
        const auto &otherDelegate = static_cast<const Delegate<TRet(Args...)> &>(other);
        if (_data.size() != otherDelegate._data.size()) {
            return false;
        }
        for (size_t i = 0; i < _data.size(); ++i) {
            if (!_data[i]->Equals(*otherDelegate._data[i])) {
                return false;
            }
        }
        return true;
    }

    template <typename U = TRet>
    typename std::enable_if<!std::is_void<U>::value, std::vector<U>>::type
    InvokeAll(Args... args) const
    {
        std::vector<U> results;
        results.reserve(_data.size());
        for (const auto &callable : _data) {
            results.emplace_back(callable->Invoke(std::forward<Args>(args)...));
        }
        return results;
    }

private:
    bool _Remove(const _ICallable &callable)
    {
        auto it  = _data.rbegin();
        auto end = _data.rend();
        while (it != end) {
            if ((*it)->Equals(callable)) {
                _data.erase(std::next(it).base());
                return true;
            }
            ++it;
        }
        return false;
    }
};

template <typename T>
using Func = Delegate<T>;

template <typename... Args>
using Action = Delegate<void(Args...)>;

#endif // _DELEGATE_H_
