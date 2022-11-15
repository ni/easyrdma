// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once


//============================================================================
//  Includes
//============================================================================
#include "tAccessManager.h"
#include "iAccessManaged.h"

enum tAccessType {
    kAccess_Shared      = 0,
    kAccess_Exclusive   = 1,
};


//============================================================================
//  Class tAccessManagedRef
//============================================================================
template <typename T>
class tAccessManagedRef {
    public:
        //--------------------------------------------------------------------
        //  Constructors
        //--------------------------------------------------------------------
        tAccessManagedRef() :
            _resource(NULL), _exclusive(false), _destructionPending(false)
        {
        }
        tAccessManagedRef(std::shared_ptr<T> resource, tAccessType access = kAccess_Exclusive, bool destructionPending = false) :
            _resource(resource),
            _exclusive(access == kAccess_Exclusive),
            _destructionPending(destructionPending)
        {
            if (_resource)
                static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Acquire(_exclusive);
        }
        tAccessManagedRef(const tAccessManagedRef& other) :
            _resource(other._resource),
            _exclusive(other._exclusive),
            _destructionPending(other._destructionPending)
        {
            if (_resource)
                static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Acquire(_exclusive);
        }
        //--------------------------------------------------------------------
        //  Destructor
        //--------------------------------------------------------------------
        ~tAccessManagedRef() {
            if (_resource)
                static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Release();
        }
        //--------------------------------------------------------------------
        //  Operators
        //--------------------------------------------------------------------
        tAccessManagedRef& operator=(const tAccessManagedRef& other) {
            if (&other == this)
                return *this;
            if (_resource)
                static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Release();
            _resource = other._resource;
            _exclusive = other._exclusive;
            _destructionPending = other._destructionPending;
            if (_resource)
                static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Acquire(_exclusive);
            return *this;
        }
        operator bool() const {
            return _resource.get() != nullptr;
        }
        T* operator->() {
            return _resource.get();
        }
        const T* operator->() const {
            return _resource.get();
        }
        void ReleaseAndWaitForAllReferencesGone() {
            assert(_resource);
            static_cast<iAccessManaged*>(_resource.get())->GetAccessManager().Release();
            _resource.get()->GetAccessManager().WaitForAllReferencesToBeReleased();
            _resource.reset();
        }
        bool IsDestructionPending() {
            return _destructionPending;
        }

        //--------------------------------------------------------------------
        //  Methods
        //--------------------------------------------------------------------
        std::shared_ptr<T> GetResource() { return _resource; }
    private:
        //--------------------------------------------------------------------
        //  Members
        //--------------------------------------------------------------------
        std::shared_ptr<T> _resource;
        bool _exclusive;
        bool _destructionPending;
};
