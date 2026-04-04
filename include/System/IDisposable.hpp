//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace System {

class IDisposable {
protected:
    virtual ~IDisposable() = default;
    virtual void Dispose(bool disposing) = 0;

};

} // System

