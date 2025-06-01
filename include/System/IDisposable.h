//
// Created by robertvokac on 6/1/25.
//

#ifndef IDISPOSABLE_H
#define IDISPOSABLE_H

namespace System {

class IDisposable {
protected:
    virtual ~IDisposable() = default;
    virtual void Dispose(bool disposing) = 0;

};

} // System

#endif //IDISPOSABLE_H
