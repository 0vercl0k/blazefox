/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_Boolean_h
#define builtin_Boolean_h

/*
 * JS boolean interface.
 */

#include "NamespaceImports.h"

namespace js {

class GlobalObject;

extern JSObject*
InitBooleanClass(JSContext* cx, js::Handle<GlobalObject*> global);

extern JSString*
BooleanToString(JSContext* cx, bool b);

} // namespace js

#endif /* builtin_Boolean_h */
