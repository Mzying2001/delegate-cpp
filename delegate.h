#ifndef _DELEGATE_H_
#define _DELEGATE_H_

#include <cstring>
#include <memory>
#include <vector>
#include <exception>

template <typename>
class Delegate;

template <typename TRet, typename... Args>
class Delegate<TRet(Args...)> final
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
        alignas(TCallableObject) char _buf[sizeof(TCallableObject)];
        _CallableObjectWrapper(const TCallableObject &obj)
        {
            memset(_buf, 0, sizeof(_buf));
            new (_buf) TCallableObject(obj);
        }
        _CallableObjectWrapper(const _CallableObjectWrapper &other)
        {
            memset(_buf, 0, sizeof(_buf));
            new (_buf) TCallableObject(other.GetObject());
        }
        virtual ~_CallableObjectWrapper()
        {
            GetObject().~TCallableObject();
        }
        TCallableObject &GetObject()
        {
            return *reinterpret_cast<TCallableObject *>(_buf);
        }
        const TCallableObject &GetObject() const
        {
            return *reinterpret_cast<const TCallableObject *>(_buf);
        }
        virtual TRet Invoke(Args... args) const override
        {
            return GetObject()(std::forward<Args>(args)...);
        }
        virtual _ICallable *Clone() const override
        {
            return new _CallableObjectWrapper(*this);
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
                return *reinterpret_cast<const Delegate<TRet(Args...)> *>(_buf) ==
                       *reinterpret_cast<const Delegate<TRet(Args...)> *>(static_cast<const _CallableObjectWrapper &>(other)._buf);
            } else {
                // Unknown type, could be a function pointer, lambda, or other type.
                // Comparing function pointers and lambdas without captured variables is generally safe,
                // since they can be converted to function pointers and have well-defined layouts.
                // However, using memcpy to compare lambdas with captured variables or custom types
                // is undefined behavior, as their memory layouts are not standardized in the C++ specification.
                return memcmp(_buf, static_cast<const _CallableObjectWrapper &>(other)._buf, sizeof(_buf)) == 0;
            }
        }
    };

    template <typename TObject>
    struct _MemberFunctionWrapper : _ICallable {
        TObject *_pObj;
        TRet (TObject::*_func)(Args...);
        _MemberFunctionWrapper(TObject &obj, TRet (TObject::*func)(Args...))
            : _pObj(&obj), _func(func)
        {
        }
        virtual TRet Invoke(Args... args) const override
        {
            return (_pObj->*_func)(std::forward<Args>(args)...);
        }
        virtual _ICallable *Clone() const override
        {
            return new _MemberFunctionWrapper(*_pObj, _func);
        }
        virtual const std::type_info *GetTypeInfo() const override
        {
            return &typeid(_func);
        }
        virtual bool Equals(const _ICallable &other) const override
        {
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            auto &mfw = static_cast<const _MemberFunctionWrapper &>(other);
            return _pObj == mfw._pObj && _func == mfw._func;
        }
    };

    template <typename TObject>
    struct _ConstMemberFunctionWrapper : _ICallable {
        const TObject *_pObj;
        TRet (TObject::*_func)(Args...) const;
        _ConstMemberFunctionWrapper(const TObject &obj, TRet (TObject::*func)(Args...) const)
            : _pObj(&obj), _func(func)
        {
        }
        virtual TRet Invoke(Args... args) const override
        {
            return (_pObj->*_func)(std::forward<Args>(args)...);
        }
        virtual _ICallable *Clone() const override
        {
            return new _ConstMemberFunctionWrapper(*_pObj, _func);
        }
        virtual const std::type_info *GetTypeInfo() const override
        {
            return &typeid(_func);
        }
        virtual bool Equals(const _ICallable &other) const override
        {
            if (GetTypeInfo() != other.GetTypeInfo()) {
                return false;
            }
            auto &cmfw = static_cast<const _ConstMemberFunctionWrapper &>(other);
            return _pObj == cmfw._pObj && _func == cmfw._func;
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

    template <typename TObject>
    Delegate(TObject &obj, TRet (TObject::*func)(Args...))
    {
        Add(obj, func);
    }

    template <typename TObject>
    Delegate(const TObject &obj, TRet (TObject::*func)(Args...) const)
    {
        Add(obj, func);
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

    template <typename TObject>
    void Add(TObject &obj, TRet (TObject::*func)(Args...))
    {
        if (func) {
            _funcs.emplace_back(new _MemberFunctionWrapper<TObject>(obj, func));
        }
    }

    template <typename TObject>
    void Add(const TObject &obj, TRet (TObject::*func)(Args...) const)
    {
        if (func) {
            _funcs.emplace_back(new _ConstMemberFunctionWrapper<TObject>(obj, func));
        }
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
        _Remove(wrapper);
    }

    void Remove(TRet (*ptr)(Args...))
    {
        if (ptr) {
            _CallableObjectWrapper<decltype(ptr)> wrapper(ptr);
            _Remove(wrapper);
        }
    }

    void Remove(std::nullptr_t)
    {
    }

    template <typename TObject>
    void Remove(TObject &obj, TRet (TObject::*func)(Args...))
    {
        if (func) {
            _MemberFunctionWrapper<TObject> wrapper(obj, func);
            _Remove(wrapper);
        }
    }

    template <typename TObject>
    void Remove(const TObject &obj, TRet (TObject::*func)(Args...) const)
    {
        if (func) {
            _ConstMemberFunctionWrapper<TObject> wrapper(obj, func);
            _Remove(wrapper);
        }
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

private:
    void _Remove(_ICallable &callable)
    {
        for (size_t i = _funcs.size(); i > 0; --i) {
            if (_funcs[i - 1]->Equals(callable)) {
                _funcs.erase(_funcs.begin() + (i - 1));
                return;
            }
        }
    }
};

template <typename T>
using Func = Delegate<T>;

template <typename... Args>
using Action = Delegate<void(Args...)>;

#endif // _DELEGATE_H_
