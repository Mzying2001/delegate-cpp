#ifndef _DELEGATE_H_
#define _DELEGATE_H_

#include <cstring>
#include <memory>
#include <vector>
#include <exception>

template <typename>
class Delegate;

template <typename TRet, typename... Args>
class Delegate<TRet(Args...)>
{
private:
    struct _ICallable {
        virtual ~_ICallable()                              = default;
        virtual TRet Invoke(Args... args) const            = 0;
        virtual _ICallable *Clone() const                  = 0;
        virtual const std::type_info *GetTypeInfo() const  = 0;
        virtual bool Equals(const _ICallable &other) const = 0;
    };

    template <typename TCallableObject>
    struct _CallableObjectWrapper : _ICallable {
        TCallableObject _obj;
        _CallableObjectWrapper(const TCallableObject &obj)
            : _obj(obj)
        {
        }
        virtual TRet Invoke(Args... args) const override
        {
            return _obj(std::forward<Args>(args)...);
        }
        virtual _ICallable *Clone() const override
        {
            return new _CallableObjectWrapper(_obj);
        }
        virtual const std::type_info *GetTypeInfo() const override
        {
            return &typeid(TCallableObject);
        }
        virtual bool Equals(const _ICallable &other) const override
        {
            auto typeinfo = GetTypeInfo();
            if (typeinfo != other.GetTypeInfo()) {
                return false;
            }
            if (typeinfo == &typeid(Delegate<TRet(Args...)>)) {
                return *reinterpret_cast<const Delegate<TRet(Args...)> *>(reinterpret_cast<const void *>(&_obj)) ==
                       *reinterpret_cast<const Delegate<TRet(Args...)> *>(reinterpret_cast<const void *>(&static_cast<const _CallableObjectWrapper &>(other)._obj));
            } else {
                // Unknown type, could be a function pointer, lambda, or other type.
                // Comparing function pointers and lambdas without captured variables is generally safe,
                // since they can be converted to function pointers and have well-defined layouts.
                // However, using memcpy to compare lambdas with captured variables or custom types
                // is undefined behavior, as their memory layouts are not standardized in the C++ specification.
                return memcmp(&_obj, &static_cast<const _CallableObjectWrapper &>(other)._obj, sizeof(_obj)) == 0;
            }
        }
    };

private:
    std::vector<std::unique_ptr<_ICallable>> _funcs;

public:
    Delegate(std::nullptr_t = nullptr)
    {
    }

    Delegate(const Delegate &other)
    {
        _funcs.reserve(other._funcs.size());
        for (auto &item : other._funcs) {
            _funcs.emplace_back(item->Clone());
        }
    }

    Delegate(Delegate &&other)
    {
        _funcs = std::move(other._funcs);
    }

    template <typename TCallableObject>
    Delegate(const TCallableObject &callable)
    {
        Add(callable);
    }

    TRet operator()(Args... args) const
    {
        if (_funcs.empty()) {
            throw std::runtime_error("empty delegate");
        }
        for (size_t i = 0; i < _funcs.size() - 1; ++i) {
            _funcs[i]->Invoke(std::forward<Args>(args)...);
        }
        return _funcs.back()->Invoke(std::forward<Args>(args)...);
    }

    TRet Invoke(Args... args) const
    {
        return (*this)(std::forward<Args>(args)...);
    }

    Delegate &operator=(const Delegate &other)
    {
        if (this == &other) {
            return *this;
        }
        _funcs.clear();
        _funcs.reserve(other._funcs.size());
        for (auto &item : other._funcs) {
            _funcs.emplace_back(item->Clone());
        }
        return *this;
    }

    Delegate &operator=(Delegate &&other)
    {
        if (this != &other) {
            _funcs = std::move(other._funcs);
        }
        return *this;
    }

    void Clear()
    {
        _funcs.clear();
    }

    Delegate &operator=(std::nullptr_t)
    {
        _funcs.clear();
        return *this;
    }

    bool IsNull() const
    {
        return _funcs.empty();
    }

    bool operator==(std::nullptr_t) const
    {
        return _funcs.empty();
    }

    bool operator!=(std::nullptr_t) const
    {
        return !_funcs.empty();
    }

    template <typename TCallableObject>
    void Add(const TCallableObject &callable)
    {
        _funcs.emplace_back(new _CallableObjectWrapper<TCallableObject>(callable));
    }

    void Add(TRet (*ptr)(Args...))
    {
        if (ptr) {
            _funcs.emplace_back(new _CallableObjectWrapper<decltype(ptr)>(ptr));
        }
    }

    void Add(std::nullptr_t)
    {
    }

    template <typename TCallableObject>
    Delegate &operator+=(const TCallableObject &callable)
    {
        Add(callable);
        return *this;
    }

    Delegate &operator+=(TRet (*ptr)(Args...))
    {
        Add(ptr);
        return *this;
    }

    Delegate &operator+=(std::nullptr_t)
    {
        return *this;
    }

    template <typename TCallableObject>
    void Remove(const TCallableObject &callable)
    {
        _CallableObjectWrapper<TCallableObject> wrapper(callable);
        for (size_t i = _funcs.size(); i > 0; --i) {
            if (_funcs[i - 1]->Equals(wrapper)) {
                _funcs.erase(_funcs.begin() + (i - 1));
                return;
            }
        }
    }

    void Remove(TRet (*ptr)(Args...))
    {
        if (ptr == nullptr) {
            return;
        }
        _CallableObjectWrapper<decltype(ptr)> wrapper(ptr);
        for (size_t i = _funcs.size(); i > 0; --i) {
            if (_funcs[i - 1]->Equals(wrapper)) {
                _funcs.erase(_funcs.begin() + (i - 1));
                return;
            }
        }
    }

    void Remove(std::nullptr_t)
    {
    }

    template <typename TCallableObject>
    Delegate &operator-=(const TCallableObject &callable)
    {
        Remove(callable);
        return *this;
    }

    Delegate &operator-=(TRet (*ptr)(Args...))
    {
        Remove(ptr);
        return *this;
    }

    Delegate &operator-=(std::nullptr_t)
    {
        return *this;
    }

    bool operator==(const Delegate &other) const
    {
        if (this == &other) {
            return true;
        }
        if (_funcs.size() != other._funcs.size()) {
            return false;
        }
        for (size_t i = 0; i < _funcs.size(); ++i) {
            if (!_funcs[i]->Equals(*other._funcs[i])) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const Delegate &other) const
    {
        return !(*this == other);
    }
};

template <typename T>
using Func = Delegate<T>;

template <typename... Args>
using Action = Delegate<void(Args...)>;

#endif // _DELEGATE_H_
