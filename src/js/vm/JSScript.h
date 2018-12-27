/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JS script descriptor. */

#ifndef vm_JSScript_h
#define vm_JSScript_h

#include "mozilla/ArrayUtils.h"
#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/MaybeOneOf.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Span.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Utf8.h"
#include "mozilla/Variant.h"

#include <type_traits> // std::is_same
#include <utility> // std::move

#include "jstypes.h"

#include "frontend/BinSourceRuntimeSupport.h"
#include "frontend/NameAnalysisTypes.h"
#include "gc/Barrier.h"
#include "gc/Rooting.h"
#include "jit/IonCode.h"
#include "js/CompileOptions.h"
#include "js/UbiNode.h"
#include "js/UniquePtr.h"
#include "js/Utility.h"
#include "vm/BytecodeUtil.h"
#include "vm/JSAtom.h"
#include "vm/NativeObject.h"
#include "vm/Scope.h"
#include "vm/Shape.h"
#include "vm/SharedImmutableStringsCache.h"
#include "vm/Time.h"

namespace JS {
struct ScriptSourceInfo;
} // namespace JS

namespace js {

namespace jit {
    struct BaselineScript;
    struct IonScriptCounts;
} // namespace jit

# define ION_DISABLED_SCRIPT ((js::jit::IonScript*)0x1)
# define ION_COMPILING_SCRIPT ((js::jit::IonScript*)0x2)
# define ION_PENDING_SCRIPT ((js::jit::IonScript*)0x3)

# define BASELINE_DISABLED_SCRIPT ((js::jit::BaselineScript*)0x1)

class AutoKeepTypeScripts;
class AutoSweepTypeScript;
class BreakpointSite;
class Debugger;
class LazyScript;
class ModuleObject;
class RegExpObject;
class SourceCompressionTask;
class Shape;
class TypeScript;

namespace frontend {
    struct BytecodeEmitter;
    class FunctionBox;
    class ModuleSharedContext;
} // namespace frontend

namespace detail {

// Do not call this directly! It is exposed for the friend declarations in
// this file.
bool
CopyScript(JSContext* cx, HandleScript src, HandleScript dst,
           MutableHandle<GCVector<Scope*>> scopes);

} // namespace detail

} // namespace js

/*
 * Type of try note associated with each catch or finally block, and also with
 * for-in and other kinds of loops. Non-for-in loops do not need these notes
 * for exception unwinding, but storing their boundaries here is helpful for
 * heuristics that need to know whether a given op is inside a loop.
 */
enum JSTryNoteKind {
    JSTRY_CATCH,
    JSTRY_FINALLY,
    JSTRY_FOR_IN,
    JSTRY_FOR_OF,
    JSTRY_LOOP,
    JSTRY_FOR_OF_ITERCLOSE,
    JSTRY_DESTRUCTURING_ITERCLOSE
};

/*
 * Exception handling record.
 */
struct JSTryNote {
    uint8_t         kind;       /* one of JSTryNoteKind */
    uint32_t        stackDepth; /* stack depth upon exception handler entry */
    uint32_t        start;      /* start of the try statement or loop
                                   relative to script->main */
    uint32_t        length;     /* length of the try statement or loop */
};

namespace js {

// A block scope has a range in bytecode: it is entered at some offset, and left
// at some later offset.  Scopes can be nested.  Given an offset, the
// ScopeNote containing that offset whose with the highest start value
// indicates the block scope.  The block scope list is sorted by increasing
// start value.
//
// It is possible to leave a scope nonlocally, for example via a "break"
// statement, so there may be short bytecode ranges in a block scope in which we
// are popping the block chain in preparation for a goto.  These exits are also
// nested with respect to outer scopes.  The scopes in these exits are indicated
// by the "index" field, just like any other block.  If a nonlocal exit pops the
// last block scope, the index will be NoScopeIndex.
//
struct ScopeNote {
    // Sentinel index for no Scope.
    static const uint32_t NoScopeIndex = UINT32_MAX;

    // Sentinel index for no ScopeNote.
    static const uint32_t NoScopeNoteIndex = UINT32_MAX;

    uint32_t        index;      // Index of Scope in the scopes array, or
                                // NoScopeIndex if there is no block scope in
                                // this range.
    uint32_t        start;      // Bytecode offset at which this scope starts,
                                // from script->main().
    uint32_t        length;     // Bytecode length of scope.
    uint32_t        parent;     // Index of parent block scope in notes, or NoScopeNote.
};

struct ConstArray {
    js::GCPtrValue* vector;     // array of indexed constant values
    uint32_t length;
};

struct ObjectArray {
    js::GCPtrObject* vector;    // Array of indexed objects.
    uint32_t length;            // Count of indexed objects.
};

struct ScopeArray {
    js::GCPtrScope* vector;     // Array of indexed scopes.
    uint32_t        length;     // Count of indexed scopes.
};

struct TryNoteArray {
    JSTryNote*      vector;     // Array of indexed try notes.
    uint32_t        length;     // Count of indexed try notes.
};

struct ScopeNoteArray {
    ScopeNote* vector;          // Array of indexed ScopeNote records.
    uint32_t   length;          // Count of indexed try notes.
};

class YieldAndAwaitOffsetArray {
    friend bool
    detail::CopyScript(JSContext* cx, HandleScript src, HandleScript dst,
                       MutableHandle<GCVector<Scope*>> scopes);

    uint32_t*       vector_;    // Array of bytecode offsets.
    uint32_t        length_;    // Count of bytecode offsets.

  public:
    void init(uint32_t* vector, uint32_t length) {
        vector_ = vector;
        length_ = length;
    }
    uint32_t& operator[](uint32_t index) {
        MOZ_ASSERT(index < length_);
        return vector_[index];
    }
    uint32_t length() const {
        return length_;
    }
};

class ScriptCounts
{
  public:
    typedef mozilla::Vector<PCCounts, 0, SystemAllocPolicy> PCCountsVector;

    inline ScriptCounts();
    inline explicit ScriptCounts(PCCountsVector&& jumpTargets);
    inline ScriptCounts(ScriptCounts&& src);
    inline ~ScriptCounts();

    inline ScriptCounts& operator=(ScriptCounts&& src);

    // Return the counter used to count the number of visits. Returns null if
    // the element is not found.
    PCCounts* maybeGetPCCounts(size_t offset);
    const PCCounts* maybeGetPCCounts(size_t offset) const;

    // PCCounts are stored at jump-target offsets. This function looks for the
    // previous PCCount which is in the same basic block as the current offset.
    PCCounts* getImmediatePrecedingPCCounts(size_t offset);

    // Return the counter used to count the number of throws. Returns null if
    // the element is not found.
    const PCCounts* maybeGetThrowCounts(size_t offset) const;

    // Throw counts are stored at the location of each throwing
    // instruction. This function looks for the previous throw count.
    //
    // Note: if the offset of the returned count is higher than the offset of
    // the immediate preceding PCCount, then this throw happened in the same
    // basic block.
    const PCCounts* getImmediatePrecedingThrowCounts(size_t offset) const;

    // Return the counter used to count the number of throws. Allocate it if
    // none exists yet. Returns null if the allocation failed.
    PCCounts* getThrowCounts(size_t offset);

    size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  private:
    friend class ::JSScript;
    friend struct ScriptAndCounts;

    // This sorted array is used to map an offset to the number of times a
    // branch got visited.
    PCCountsVector pcCounts_;

    // This sorted vector is used to map an offset to the number of times an
    // instruction throw.
    PCCountsVector throwCounts_;

    // Information about any Ion compilations for the script.
    jit::IonScriptCounts* ionCounts_;
};

// Note: The key of this hash map is a weak reference to a JSScript.  We do not
// use the WeakMap implementation provided in gc/WeakMap.h because it would be
// collected at the beginning of the sweeping of the realm, thus before the
// calls to the JSScript::finalize function which are used to aggregate code
// coverage results on the realm.
using UniqueScriptCounts = js::UniquePtr<ScriptCounts>;
using ScriptCountsMap = HashMap<JSScript*,
                                UniqueScriptCounts,
                                DefaultHasher<JSScript*>,
                                SystemAllocPolicy>;

using ScriptNameMap = HashMap<JSScript*,
                              JS::UniqueChars,
                              DefaultHasher<JSScript*>,
                              SystemAllocPolicy>;

class DebugScript
{
    friend class ::JSScript;
    friend class JS::Realm;

    /*
     * When non-zero, compile script in single-step mode. The top bit is set and
     * cleared by setStepMode, as used by JSD. The lower bits are a count,
     * adjusted by changeStepModeCount, used by the Debugger object. Only
     * when the bit is clear and the count is zero may we compile the script
     * without single-step support.
     */
    uint32_t        stepMode;

    /*
     * Number of breakpoint sites at opcodes in the script. This is the number
     * of populated entries in DebugScript::breakpoints, below.
     */
    uint32_t        numSites;

    /*
     * Breakpoints set in our script. For speed and simplicity, this array is
     * parallel to script->code(): the BreakpointSite for the opcode at
     * script->code()[offset] is debugScript->breakpoints[offset]. Naturally,
     * this array's true length is script->length().
     */
    BreakpointSite* breakpoints[1];
};

using UniqueDebugScript = js::UniquePtr<DebugScript, JS::FreePolicy>;
using DebugScriptMap = HashMap<JSScript*,
                               UniqueDebugScript,
                               DefaultHasher<JSScript*>,
                               SystemAllocPolicy>;

class ScriptSource;

struct ScriptSourceChunk
{
    ScriptSource* ss = nullptr;
    uint32_t chunk = 0;

    ScriptSourceChunk() = default;

    ScriptSourceChunk(ScriptSource* ss, uint32_t chunk)
      : ss(ss), chunk(chunk)
    {
        MOZ_ASSERT(valid());
    }

    bool valid() const { return ss != nullptr; }

    bool operator==(const ScriptSourceChunk& other) const {
        return ss == other.ss && chunk == other.chunk;
    }
};

struct ScriptSourceChunkHasher
{
    using Lookup = ScriptSourceChunk;

    static HashNumber hash(const ScriptSourceChunk& ssc) {
        return mozilla::AddToHash(DefaultHasher<ScriptSource*>::hash(ssc.ss), ssc.chunk);
    }
    static bool match(const ScriptSourceChunk& c1, const ScriptSourceChunk& c2) {
        return c1 == c2;
    }
};

template<typename Unit>
using EntryUnits = mozilla::UniquePtr<Unit[], JS::FreePolicy>;

// The uncompressed source cache contains *either* UTF-8 source data *or*
// UTF-16 source data.  ScriptSourceChunk implies a ScriptSource that
// contains either UTF-8 data or UTF-16 data, so the nature of the key to
// Map below indicates how each SourceData ought to be interpreted.
using SourceData = mozilla::UniquePtr<void, JS::FreePolicy>;

template<typename Unit>
inline SourceData
ToSourceData(EntryUnits<Unit> chars)
{
    static_assert(std::is_same<SourceData::DeleterType,
                               typename EntryUnits<Unit>::DeleterType>::value,
                  "EntryUnits and SourceData must share the same deleter "
                  "type, that need not know the type of the data being freed, "
                  "for the upcast below to be safe");
    return SourceData(chars.release());
}

class UncompressedSourceCache
{
    using Map = HashMap<ScriptSourceChunk,
                        SourceData,
                        ScriptSourceChunkHasher,
                        SystemAllocPolicy>;

  public:
    // Hold an entry in the source data cache and prevent it from being purged on GC.
    class AutoHoldEntry
    {
        UncompressedSourceCache* cache_ = nullptr;
        ScriptSourceChunk sourceChunk_ = {};
        SourceData data_ = nullptr;

      public:
        explicit AutoHoldEntry() = default;

        ~AutoHoldEntry() {
            if (cache_) {
                MOZ_ASSERT(sourceChunk_.valid());
                cache_->releaseEntry(*this);
            }
        }

        template<typename Unit>
        void holdUnits(EntryUnits<Unit> units) {
            MOZ_ASSERT(!cache_);
            MOZ_ASSERT(!sourceChunk_.valid());
            MOZ_ASSERT(!data_);

            data_ = ToSourceData(std::move(units));
        }

      private:
        void holdEntry(UncompressedSourceCache* cache, const ScriptSourceChunk& sourceChunk) {
            // Initialise the holder for a specific cache and script source.
            // This will hold on to the cached source chars in the event that
            // the cache is purged.
            MOZ_ASSERT(!cache_);
            MOZ_ASSERT(!sourceChunk_.valid());
            MOZ_ASSERT(!data_);

            cache_ = cache;
            sourceChunk_ = sourceChunk;
        }

        void deferDelete(SourceData data) {
            // Take ownership of source chars now the cache is being purged. Remove our
            // reference to the ScriptSource which might soon be destroyed.
            MOZ_ASSERT(cache_);
            MOZ_ASSERT(sourceChunk_.valid());
            MOZ_ASSERT(!data_);

            cache_ = nullptr;
            sourceChunk_ = ScriptSourceChunk();

            data_ = std::move(data);
        }


        const ScriptSourceChunk& sourceChunk() const { return sourceChunk_; }
        friend class UncompressedSourceCache;
    };

  private:
    UniquePtr<Map> map_ = nullptr;
    AutoHoldEntry* holder_ = nullptr;

  public:
    UncompressedSourceCache() = default;

    template<typename Unit>
    const Unit* lookup(const ScriptSourceChunk& ssc, AutoHoldEntry& asp);

    bool put(const ScriptSourceChunk& ssc, SourceData data, AutoHoldEntry& asp);

    void purge();

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);

  private:
    void holdEntry(AutoHoldEntry& holder, const ScriptSourceChunk& ssc);
    void releaseEntry(AutoHoldEntry& holder);
};

template<typename Unit>
struct SourceTypeTraits;

template<>
struct SourceTypeTraits<mozilla::Utf8Unit>
{
    using CharT = char;
    using SharedImmutableString = js::SharedImmutableString;

    static const mozilla::Utf8Unit* units(const SharedImmutableString& string) {
        // Casting |char| data to |Utf8Unit| is safe because |Utf8Unit|
        // contains a |char|.  See the long comment in |Utf8Unit|'s definition.
        return reinterpret_cast<const mozilla::Utf8Unit*>(string.chars());
    }

    static char* toString(const mozilla::Utf8Unit* units) {
        auto asUnsigned = const_cast<unsigned char*>(mozilla::Utf8AsUnsignedChars(units));
        return reinterpret_cast<char*>(asUnsigned);
    }

    static UniqueChars toCacheable(EntryUnits<mozilla::Utf8Unit> str) {
        // The cache only stores strings of |char| or |char16_t|, and right now
        // it seems best not to gunk up the cache with |Utf8Unit| too.  So
        // cache |Utf8Unit| strings by interpreting them as |char| strings.
        char* chars = toString(str.release());
        return UniqueChars(chars);
    }
};

template<>
struct SourceTypeTraits<char16_t>
{
    using CharT = char16_t;
    using SharedImmutableString = js::SharedImmutableTwoByteString;

    static const char16_t* units(const SharedImmutableString& string) {
        return string.chars();
    }

    static char16_t* toString(const char16_t* units) {
        return const_cast<char16_t*>(units);
    }

    static UniqueTwoByteChars toCacheable(EntryUnits<char16_t> str) {
        return UniqueTwoByteChars(std::move(str));
    }
};

class ScriptSource
{
    friend class SourceCompressionTask;

    class PinnedUnitsBase
    {
      protected:
        PinnedUnitsBase** stack_ = nullptr;
        PinnedUnitsBase* prev_ = nullptr;

        ScriptSource* source_;

        explicit PinnedUnitsBase(ScriptSource* source)
          : source_(source)
        {}
    };

  public:
    // Any users that wish to manipulate the char buffer of the ScriptSource
    // needs to do so via PinnedUnits for GC safety. A GC may compress
    // ScriptSources. If the source were initially uncompressed, then any raw
    // pointers to the char buffer would now point to the freed, uncompressed
    // chars. This is analogous to Rooted.
    template<typename Unit>
    class PinnedUnits : public PinnedUnitsBase
    {
        const Unit* units_;

      public:
        PinnedUnits(JSContext* cx, ScriptSource* source,
                    UncompressedSourceCache::AutoHoldEntry& holder,
                    size_t begin, size_t len);

        ~PinnedUnits();

        const Unit* get() const {
            return units_;
        }

        const typename SourceTypeTraits<Unit>::CharT* asChars() const {
            return SourceTypeTraits<Unit>::toString(get());
        }
    };

  private:
    mozilla::Atomic<uint32_t, mozilla::ReleaseAcquire,
                    mozilla::recordreplay::Behavior::DontPreserve> refs;

    // Note: while ScriptSources may be compressed off thread, they are only
    // modified by the main thread, and all members are always safe to access
    // on the main thread.

    // Indicate which field in the |data| union is active.
    struct Missing { };

    template<typename Unit>
    class Uncompressed
    {
        typename SourceTypeTraits<Unit>::SharedImmutableString string_;


      public:
        explicit Uncompressed(typename SourceTypeTraits<Unit>::SharedImmutableString str)
          : string_(std::move(str))
        {}

        const Unit* units() const {
            return SourceTypeTraits<Unit>::units(string_);
        }

        size_t length() const {
            return string_.length();
        }
    };

    template<typename Unit>
    struct Compressed
    {
        // Single-byte compressed text, regardless whether the original text
        // was single-byte or two-byte.
        SharedImmutableString raw;
        size_t uncompressedLength;

        Compressed(SharedImmutableString raw, size_t uncompressedLength)
          : raw(std::move(raw)),
            uncompressedLength(uncompressedLength)
        { }
    };

    struct BinAST
    {
        SharedImmutableString string;
        explicit BinAST(SharedImmutableString&& str)
          : string(std::move(str))
        { }
    };

    using SourceType =
        mozilla::Variant<Compressed<mozilla::Utf8Unit>, Uncompressed<mozilla::Utf8Unit>,
                         Compressed<char16_t>, Uncompressed<char16_t>,
                         Missing,
                         BinAST>;
    SourceType data;

    // If the GC attempts to call setCompressedSource with PinnedUnits
    // present, the first PinnedUnits (that is, bottom of the stack) will set
    // the compressed chars upon destruction.
    PinnedUnitsBase* pinnedUnitsStack_;
    mozilla::MaybeOneOf<Compressed<mozilla::Utf8Unit>, Compressed<char16_t>> pendingCompressed_;

    // The filename of this script.
    UniqueChars filename_;

    UniqueTwoByteChars displayURL_;
    UniqueTwoByteChars sourceMapURL_;
    bool mutedErrors_;

    // bytecode offset in caller script that generated this code.
    // This is present for eval-ed code, as well as "new Function(...)"-introduced
    // scripts.
    uint32_t introductionOffset_;

    // If this source is for Function constructor, the position of ")" after
    // parameter list in the source.  This is used to get function body.
    // 0 for other cases.
    uint32_t parameterListEnd_;

    // If this ScriptSource was generated by a code-introduction mechanism such
    // as |eval| or |new Function|, the debugger needs access to the "raw"
    // filename of the top-level script that contains the eval-ing code.  To
    // keep track of this, we must preserve the original outermost filename (of
    // the original introducer script), so that instead of a filename of
    // "foo.js line 30 > eval line 10 > Function", we can obtain the original
    // raw filename of "foo.js".
    //
    // In the case described above, this field will be non-null and will be the
    // original raw filename from above.  Otherwise this field will be null.
    UniqueChars introducerFilename_;

    // A string indicating how this source code was introduced into the system.
    // This accessor returns one of the following values:
    //      "eval" for code passed to |eval|.
    //      "Function" for code passed to the |Function| constructor.
    //      "Worker" for code loaded by calling the Web worker constructor&mdash;the worker's main script.
    //      "importScripts" for code by calling |importScripts| in a web worker.
    //      "handler" for code assigned to DOM elements' event handler IDL attributes.
    //      "scriptElement" for code belonging to <script> elements.
    //      undefined if the implementation doesn't know how the code was introduced.
    // This is a constant, statically allocated C string, so does not need
    // memory management.
    const char* introductionType_;

    // The bytecode cache encoder is used to encode only the content of function
    // which are delazified.  If this value is not nullptr, then each delazified
    // function should be recorded before their first execution.
    UniquePtr<XDRIncrementalEncoder> xdrEncoder_;

    // Instant at which the first parse of this source ended, or null
    // if the source hasn't been parsed yet.
    //
    // Used for statistics purposes, to determine how much time code spends
    // syntax parsed before being full parsed, to help determine whether
    // our syntax parse vs. full parse heuristics are correct.
    mozilla::TimeStamp parseEnded_;

    // True if we can call JSRuntime::sourceHook to load the source on
    // demand. If sourceRetrievable_ and hasSourceText() are false, it is not
    // possible to get source at all.
    bool sourceRetrievable_:1;
    bool hasIntroductionOffset_:1;
    bool containsAsmJS_:1;

    UniquePtr<frontend::BinASTSourceMetadata> binASTMetadata_;

    template<typename Unit>
    const Unit* chunkUnits(JSContext* cx, UncompressedSourceCache::AutoHoldEntry& holder,
                           size_t chunk);

    // Return a string containing the chars starting at |begin| and ending at
    // |begin + len|.
    //
    // Warning: this is *not* GC-safe! Any chars to be handed out should use
    // PinnedUnits. See comment below.
    template<typename Unit>
    const Unit* units(JSContext* cx, UncompressedSourceCache::AutoHoldEntry& asp,
                      size_t begin, size_t len);

    template<typename Unit>
    void movePendingCompressedSource();

  public:
    // When creating a JSString* from TwoByte source characters, we don't try to
    // to deflate to Latin1 for longer strings, because this can be slow.
    static const size_t SourceDeflateLimit = 100;

    explicit ScriptSource()
      : refs(0),
        data(SourceType(Missing())),
        pinnedUnitsStack_(nullptr),
        filename_(nullptr),
        displayURL_(nullptr),
        sourceMapURL_(nullptr),
        mutedErrors_(false),
        introductionOffset_(0),
        parameterListEnd_(0),
        introducerFilename_(nullptr),
        introductionType_(nullptr),
        xdrEncoder_(nullptr),
        sourceRetrievable_(false),
        hasIntroductionOffset_(false),
        containsAsmJS_(false)
    {
    }

    ~ScriptSource() {
        MOZ_ASSERT(refs == 0);
    }

    void incref() { refs++; }
    void decref() {
        MOZ_ASSERT(refs != 0);
        if (--refs == 0) {
            js_delete(this);
        }
    }
    MOZ_MUST_USE bool initFromOptions(JSContext* cx,
                                      const JS::ReadOnlyCompileOptions& options,
                                      const mozilla::Maybe<uint32_t>& parameterListEnd = mozilla::Nothing());
    MOZ_MUST_USE bool setSourceCopy(JSContext* cx, JS::SourceBufferHolder& srcBuf);
    void setSourceRetrievable() { sourceRetrievable_ = true; }
    bool sourceRetrievable() const { return sourceRetrievable_; }
    bool hasSourceText() const { return hasUncompressedSource() || hasCompressedSource(); }
    bool hasBinASTSource() const { return data.is<BinAST>(); }

    void setBinASTSourceMetadata(frontend::BinASTSourceMetadata* metadata) {
        MOZ_ASSERT(hasBinASTSource());
        binASTMetadata_.reset(metadata);
    }
    frontend::BinASTSourceMetadata* binASTSourceMetadata() const {
        MOZ_ASSERT(hasBinASTSource());
        return binASTMetadata_.get();
    }

  private:
    struct UncompressedDataMatcher
    {
        template<typename Unit>
        const void* match(const Uncompressed<Unit>& u) {
            return u.units();
        }

        template<typename T>
        const void* match(const T&) {
            MOZ_CRASH("attempting to access uncompressed data in a "
                      "ScriptSource not containing it");
            return nullptr;
        }
    };

  public:
    template<typename Unit>
    const Unit* uncompressedData() {
        return static_cast<const Unit*>(data.match(UncompressedDataMatcher()));
    }

  private:
    struct CompressedDataMatcher
    {
        template<typename Unit>
        char* match(const Compressed<Unit>& c) {
            return const_cast<char*>(c.raw.chars());
        }

        template<typename T>
        char* match(const T&) {
            MOZ_CRASH("attempting to access compressed data in a ScriptSource "
                      "not containing it");
            return nullptr;
        }
    };

  public:
    template<typename Unit>
    char* compressedData() {
        return data.match(CompressedDataMatcher());
    }

  private:
    struct BinASTDataMatcher
    {
        void* match(const BinAST& b) {
            return const_cast<char*>(b.string.chars());
        }

        void notBinAST() {
            MOZ_CRASH("ScriptSource isn't backed by BinAST data");
        }

        template<typename T>
        void* match(const T&) {
            notBinAST();
            return nullptr;
        }
    };

  public:
    void* binASTData() {
        return data.match(BinASTDataMatcher());
    }

  private:
    struct HasUncompressedSource
    {
        template<typename Unit>
        bool match(const Uncompressed<Unit>&) { return true; }

        template<typename Unit>
        bool match(const Compressed<Unit>&) { return false; }

        bool match(const BinAST&) { return false; }

        bool match(const Missing&) { return false; }
    };

  public:
    bool hasUncompressedSource() const {
        return data.match(HasUncompressedSource());
    }

    template<typename Unit>
    bool uncompressedSourceIs() const {
        MOZ_ASSERT(hasUncompressedSource());
        return data.is<Uncompressed<Unit>>();
    }

  private:
    struct HasCompressedSource
    {
        template<typename Unit>
        bool match(const Compressed<Unit>&) { return true; }

        template<typename Unit>
        bool match(const Uncompressed<Unit>&) { return false; }

        bool match(const BinAST&) { return false; }

        bool match(const Missing&) { return false; }
    };

  public:
    bool hasCompressedSource() const {
        return data.match(HasCompressedSource());
    }

    template<typename Unit>
    bool compressedSourceIs() const {
        MOZ_ASSERT(hasCompressedSource());
        return data.is<Compressed<Unit>>();
    }

  private:
    template<typename Unit>
    struct SourceTypeMatcher
    {
        template<template<typename C> class Data>
        bool match(const Data<Unit>&) {
            return true;
        }

        template<template<typename C> class Data, typename NotUnit>
        bool match(const Data<NotUnit>&) {
            return false;
        }

        bool match(const BinAST&) {
            MOZ_CRASH("doesn't make sense to ask source type of BinAST data");
            return false;
        }

        bool match(const Missing&) {
            MOZ_CRASH("doesn't make sense to ask source type when missing");
            return false;
        }
    };

  public:
    template<typename Unit>
    bool hasSourceType() const {
        return data.match(SourceTypeMatcher<Unit>());
    }

  private:
    struct SourceCharSizeMatcher
    {
        template<template<typename C> class Data, typename Unit>
        uint8_t match(const Data<Unit>& data) {
            static_assert(std::is_same<Unit, mozilla::Utf8Unit>::value ||
                          std::is_same<Unit, char16_t>::value,
                          "should only have UTF-8 or UTF-16 source char");
            return sizeof(Unit);
        }

        uint8_t match(const BinAST&) {
            MOZ_CRASH("BinAST source has no source-char size");
            return 0;
        }

        uint8_t match(const Missing&) {
            MOZ_CRASH("missing source has no source-char size");
            return 0;
        }
    };

  public:
    uint8_t sourceCharSize() const {
        return data.match(SourceCharSizeMatcher());
    }

  private:
    struct UncompressedLengthMatcher
    {
        template<typename Unit>
        size_t match(const Uncompressed<Unit>& u) {
            return u.length();
        }

        template<typename Unit>
        size_t match(const Compressed<Unit>& u) {
            return u.uncompressedLength;
        }

        size_t match(const BinAST& b) {
            return b.string.length();
        }

        size_t match(const Missing& m) {
            MOZ_CRASH("ScriptSource::length on a missing source");
            return 0;
        }
    };

  public:
    size_t length() const {
        MOZ_ASSERT(hasSourceText() || hasBinASTSource());
        return data.match(UncompressedLengthMatcher());
    }

  private:
    struct CompressedLengthOrZeroMatcher
    {
        template<typename Unit>
        size_t match(const Uncompressed<Unit>&) {
            return 0;
        }

        template<typename Unit>
        size_t match(const Compressed<Unit>& c) {
            return c.raw.length();
        }

        size_t match(const BinAST&) {
            MOZ_CRASH("trying to get compressed length for BinAST data");
            return 0;
        }

        size_t match(const Missing&) {
            MOZ_CRASH("missing source data");
            return 0;
        }
    };

  public:
    size_t compressedLengthOrZero() const {
        return data.match(CompressedLengthOrZeroMatcher());
    }

    JSFlatString* substring(JSContext* cx, size_t start, size_t stop);
    JSFlatString* substringDontDeflate(JSContext* cx, size_t start, size_t stop);

    MOZ_MUST_USE bool appendSubstring(JSContext* cx, js::StringBuffer& buf, size_t start, size_t stop);

    bool isFunctionBody() {
        return parameterListEnd_ != 0;
    }
    JSFlatString* functionBodyString(JSContext* cx);

    void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                                JS::ScriptSourceInfo* info) const;

    template<typename Unit>
    MOZ_MUST_USE bool setSource(JSContext* cx,
                                EntryUnits<Unit>&& source,
                                size_t length);

    template<typename Unit>
    void setSource(typename SourceTypeTraits<Unit>::SharedImmutableString uncompressed);

    MOZ_MUST_USE bool tryCompressOffThread(JSContext* cx);

    // The Unit parameter determines which type of compressed source is
    // recorded, but raw compressed source is always single-byte.
    template<typename Unit>
    void setCompressedSource(SharedImmutableString compressed, size_t sourceLength);

    template<typename Unit>
    MOZ_MUST_USE bool setCompressedSource(JSContext* cx, UniqueChars&& raw, size_t rawLength,
                                          size_t sourceLength);

#if defined(JS_BUILD_BINAST)

    /*
     * Do not take ownership of the given `buf`. Store the canonical, shared
     * and de-duplicated version. If there is no extant shared version of
     * `buf`, make a copy.
     */
    MOZ_MUST_USE bool setBinASTSourceCopy(JSContext* cx, const uint8_t* buf, size_t len);

    /*
     * Take ownership of the given `buf` and return the canonical, shared and
     * de-duplicated version.
     */
    MOZ_MUST_USE bool setBinASTSource(JSContext* cx, UniqueChars&& buf, size_t len);

    const uint8_t* binASTSource();

#endif /* JS_BUILD_BINAST */

  private:
    void performTaskWork(SourceCompressionTask* task);

    struct SetCompressedSourceFromTask
    {
        ScriptSource* const source_;
        SharedImmutableString& compressed_;

        SetCompressedSourceFromTask(ScriptSource* source, SharedImmutableString& compressed)
          : source_(source),
            compressed_(compressed)
        {}

        template<typename Unit>
        void match(const Uncompressed<Unit>&) {
            source_->setCompressedSource<Unit>(std::move(compressed_), source_->length());
        }

        template<typename Unit>
        void match(const Compressed<Unit>&) {
            MOZ_CRASH("can't set compressed source when source is already "
                      "compressed -- ScriptSource::tryCompressOffThread "
                      "shouldn't have queued up this task?");
        }

        void match(const BinAST&) {
            MOZ_CRASH("doesn't make sense to set compressed source for BinAST "
                      "data");
        }

        void match(const Missing&) {
            MOZ_CRASH("doesn't make sense to set compressed source for "
                      "missing source -- ScriptSource::tryCompressOffThread "
                      "shouldn't have queued up this task?");
        }
    };

    void setCompressedSourceFromTask(SharedImmutableString compressed);

  public:
    // XDR handling
    template <XDRMode mode>
    MOZ_MUST_USE XDRResult performXDR(XDRState<mode>* xdr);

  private:
    // It'd be better to make this function take <XDRMode, Unit>, as both
    // specializations of this function contain nested Unit-parametrized
    // helper classes that do everything the function needs to do.  But then
    // we'd need template function partial specialization to hold XDRMode
    // constant while varying Unit, so that idea's no dice.
    template <XDRMode mode>
    MOZ_MUST_USE XDRResult xdrUncompressedSource(XDRState<mode>* xdr, uint8_t sourceCharSize,
                                                 uint32_t uncompressedLength);

  public:
    MOZ_MUST_USE bool setFilename(JSContext* cx, const char* filename);
    const char* introducerFilename() const {
        return introducerFilename_ ? introducerFilename_.get() : filename_.get();
    }
    bool hasIntroductionType() const {
        return introductionType_;
    }
    const char* introductionType() const {
        MOZ_ASSERT(hasIntroductionType());
        return introductionType_;
    }
    const char* filename() const {
        return filename_.get();
    }

    // Display URLs
    MOZ_MUST_USE bool setDisplayURL(JSContext* cx, const char16_t* displayURL);
    bool hasDisplayURL() const { return displayURL_ != nullptr; }
    const char16_t * displayURL() {
        MOZ_ASSERT(hasDisplayURL());
        return displayURL_.get();
    }

    // Source maps
    MOZ_MUST_USE bool setSourceMapURL(JSContext* cx, const char16_t* sourceMapURL);
    bool hasSourceMapURL() const { return sourceMapURL_ != nullptr; }
    const char16_t * sourceMapURL() {
        MOZ_ASSERT(hasSourceMapURL());
        return sourceMapURL_.get();
    }

    bool mutedErrors() const { return mutedErrors_; }

    bool hasIntroductionOffset() const { return hasIntroductionOffset_; }
    uint32_t introductionOffset() const {
        MOZ_ASSERT(hasIntroductionOffset());
        return introductionOffset_;
    }
    void setIntroductionOffset(uint32_t offset) {
        MOZ_ASSERT(!hasIntroductionOffset());
        MOZ_ASSERT(offset <= (uint32_t)INT32_MAX);
        introductionOffset_ = offset;
        hasIntroductionOffset_ = true;
    }

    bool containsAsmJS() const { return containsAsmJS_; }
    void setContainsAsmJS() {
        containsAsmJS_ = true;
    }

    // Return wether an XDR encoder is present or not.
    bool hasEncoder() const { return bool(xdrEncoder_); }

    // Create a new XDR encoder, and encode the top-level JSScript. The result
    // of the encoding would be available in the |buffer| provided as argument,
    // as soon as |xdrFinalize| is called and all xdr function calls returned
    // successfully.
    bool xdrEncodeTopLevel(JSContext* cx, HandleScript script);

    // Encode a delazified JSFunction.  In case of errors, the XDR encoder is
    // freed and the |buffer| provided as argument to |xdrEncodeTopLevel| is
    // considered undefined.
    //
    // The |sourceObject| argument is the object holding the current
    // ScriptSource.
    bool xdrEncodeFunction(JSContext* cx, HandleFunction fun,
                           HandleScriptSourceObject sourceObject);

    // Linearize the encoded content in the |buffer| provided as argument to
    // |xdrEncodeTopLevel|, and free the XDR encoder.  In case of errors, the
    // |buffer| is considered undefined.
    bool xdrFinalizeEncoder(JS::TranscodeBuffer& buffer);

    const mozilla::TimeStamp parseEnded() const {
        return parseEnded_;
    }
    // Inform `this` source that it has been fully parsed.
    void recordParseEnded() {
        MOZ_ASSERT(parseEnded_.IsNull());
        parseEnded_ = ReallyNow();
    }

    void trace(JSTracer* trc);
};

class ScriptSourceHolder
{
    ScriptSource* ss;
  public:
    ScriptSourceHolder()
      : ss(nullptr)
    {}
    explicit ScriptSourceHolder(ScriptSource* ss)
      : ss(ss)
    {
        ss->incref();
    }
    ~ScriptSourceHolder()
    {
        if (ss) {
            ss->decref();
        }
    }
    void reset(ScriptSource* newss) {
        // incref before decref just in case ss == newss.
        if (newss) {
            newss->incref();
        }
        if (ss) {
            ss->decref();
        }
        ss = newss;
    }
    ScriptSource* get() const {
        return ss;
    }
};

class ScriptSourceObject : public NativeObject
{
    static const ClassOps classOps_;

  public:
    static const Class class_;

    static void trace(JSTracer* trc, JSObject* obj);
    static void finalize(FreeOp* fop, JSObject* obj);
    static ScriptSourceObject* create(JSContext* cx, ScriptSource* source);

    // Initialize those properties of this ScriptSourceObject whose values
    // are provided by |options|, re-wrapping as necessary.
    static bool initFromOptions(JSContext* cx, HandleScriptSourceObject source,
                                const JS::ReadOnlyCompileOptions& options);

    static bool initElementProperties(JSContext* cx, HandleScriptSourceObject source,
                                      HandleObject element, HandleString elementAttrName);

    bool hasSource() const {
        return !getReservedSlot(SOURCE_SLOT).isUndefined();
    }
    ScriptSource* source() const {
        return static_cast<ScriptSource*>(getReservedSlot(SOURCE_SLOT).toPrivate());
    }
    JSObject* element() const {
        return getReservedSlot(ELEMENT_SLOT).toObjectOrNull();
    }
    const Value& elementAttributeName() const {
        MOZ_ASSERT(!getReservedSlot(ELEMENT_PROPERTY_SLOT).isMagic());
        return getReservedSlot(ELEMENT_PROPERTY_SLOT);
    }
    JSScript* introductionScript() const {
        Value value = getReservedSlot(INTRODUCTION_SCRIPT_SLOT);
        if (value.isUndefined()) {
            return nullptr;
        }
        return value.toGCThing()->as<JSScript>();
    }

    void setPrivate(const Value& value) {
        setReservedSlot(PRIVATE_SLOT, value);
    }
    Value getPrivate() const {
        return getReservedSlot(PRIVATE_SLOT);
    }

  private:
    enum {
        SOURCE_SLOT = 0,
        ELEMENT_SLOT,
        ELEMENT_PROPERTY_SLOT,
        INTRODUCTION_SCRIPT_SLOT,
        PRIVATE_SLOT,
        RESERVED_SLOTS
    };
};

enum class GeneratorKind : bool { NotGenerator, Generator };
enum class FunctionAsyncKind : bool { SyncFunction, AsyncFunction };

/*
 * NB: after a successful XDR_DECODE, XDRScript callers must do any required
 * subsequent set-up of owning function or script object and then call
 * CallNewScriptHook.
 */
template<XDRMode mode>
XDRResult
XDRScript(XDRState<mode>* xdr, HandleScope enclosingScope,
          HandleScriptSourceObject sourceObject,
          HandleFunction fun, MutableHandleScript scriptp);

template<XDRMode mode>
XDRResult
XDRLazyScript(XDRState<mode>* xdr, HandleScope enclosingScope,
              HandleScriptSourceObject sourceObject,
              HandleFunction fun, MutableHandle<LazyScript*> lazy);

/*
 * Code any constant value.
 */
template<XDRMode mode>
XDRResult
XDRScriptConst(XDRState<mode>* xdr, MutableHandleValue vp);

/*
 * Common data that can be shared between many scripts in a single runtime.
 */
class SharedScriptData
{
    // This class is reference counted as follows: each pointer from a JSScript
    // counts as one reference plus there may be one reference from the shared
    // script data table.
    mozilla::Atomic<uint32_t, mozilla::SequentiallyConsistent,
                    mozilla::recordreplay::Behavior::DontPreserve> refCount_;

    uint32_t natoms_;
    uint32_t codeLength_;
    uint32_t noteLength_;
    uintptr_t data_[1];

  public:
    static SharedScriptData* new_(JSContext* cx, uint32_t codeLength,
                                  uint32_t srcnotesLength, uint32_t natoms);

    uint32_t refCount() const {
        return refCount_;
    }
    void incRefCount() {
        refCount_++;
    }
    void decRefCount() {
        MOZ_ASSERT(refCount_ != 0);
        uint32_t remain = --refCount_;
        if (remain == 0) {
            js_free(this);
        }
    }

    size_t dataLength() const {
        return (natoms_ * sizeof(GCPtrAtom)) + codeLength_ + noteLength_;
    }
    const uint8_t* data() const {
        return reinterpret_cast<const uint8_t*>(data_);
    }
    uint8_t* data() {
        return reinterpret_cast<uint8_t*>(data_);
    }

    uint32_t natoms() const {
        return natoms_;
    }
    GCPtrAtom* atoms() {
        if (!natoms_) {
            return nullptr;
        }
        return reinterpret_cast<GCPtrAtom*>(data());
    }

    uint32_t codeLength() const {
        return codeLength_;
    }
    jsbytecode* code() {
        return reinterpret_cast<jsbytecode*>(data() + natoms_ * sizeof(GCPtrAtom));
    }

    uint32_t numNotes() const {
        return noteLength_;
    }
    jssrcnote* notes() {
        return reinterpret_cast<jssrcnote*>(data() + natoms_ * sizeof(GCPtrAtom) + codeLength_);
    }

    void traceChildren(JSTracer* trc);

  private:
    SharedScriptData() = delete;
    SharedScriptData(const SharedScriptData&) = delete;
    SharedScriptData& operator=(const SharedScriptData&) = delete;
};

struct ScriptBytecodeHasher
{
    class Lookup {
        friend struct ScriptBytecodeHasher;

        SharedScriptData* scriptData;
        HashNumber hash;

      public:
        explicit Lookup(SharedScriptData* data);
        ~Lookup();
    };

    static HashNumber hash(const Lookup& l) {
        return l.hash;
    }
    static bool match(SharedScriptData* entry, const Lookup& lookup) {
        const SharedScriptData* data = lookup.scriptData;
        if (entry->natoms() != data->natoms()) {
            return false;
        }
        if (entry->codeLength() != data->codeLength()) {
            return false;
        }
        if (entry->numNotes() != data->numNotes()) {
            return false;
        }
        return mozilla::ArrayEqual<uint8_t>(entry->data(), data->data(), data->dataLength());
    }
};

class AutoLockScriptData;

using ScriptDataTable = HashSet<SharedScriptData*,
                                ScriptBytecodeHasher,
                                SystemAllocPolicy>;

extern void
SweepScriptData(JSRuntime* rt);

extern void
FreeScriptData(JSRuntime* rt);

} /* namespace js */

class JSScript : public js::gc::TenuredCell
{
  private:
    // Pointer to baseline->method()->raw(), ion->method()->raw(), a wasm jit
    // entry, the JIT's EnterInterpreter stub, or the lazy link stub. Must be
    // non-null.
    uint8_t* jitCodeRaw_ = nullptr;
    uint8_t* jitCodeSkipArgCheck_ = nullptr;

    js::SharedScriptData* scriptData_ = nullptr;

  public:
    // Pointer to variable-length data array (see comment above Create() for
    // details).
    uint8_t* data = nullptr;

    JS::Realm* realm_ = nullptr;

  private:
    /* Persistent type information retained across GCs. */
    js::TypeScript* types_ = nullptr;

    // This script's ScriptSourceObject, or a CCW thereof.
    //
    // (When we clone a JSScript into a new compartment, we don't clone its
    // source object. Instead, the clone refers to a wrapper.)
    js::GCPtrObject sourceObject_ = {};

    /*
     * Information attached by Ion. Nexto a valid IonScript this could be
     * ION_DISABLED_SCRIPT, ION_COMPILING_SCRIPT or ION_PENDING_SCRIPT.
     * The later is a ion compilation that is ready, but hasn't been linked
     * yet.
     */
    js::jit::IonScript* ion = nullptr;

    /* Information attached by Baseline. */
    js::jit::BaselineScript* baseline = nullptr;

    /* Information used to re-lazify a lazily-parsed interpreted function. */
    js::LazyScript* lazyScript = nullptr;

    // 32-bit fields.

    /* Size of the used part of the data array. */
    uint32_t dataSize_ = 0;

    /* Base line number of script. */
    uint32_t lineno_ = 0;

    /* Base column of script, optionally set. */
    uint32_t column_ = 0;

    /* Offset of main entry point from code, after predef'ing prologue. */
    uint32_t mainOffset_ = 0;

    /* Fixed frame slots. */
    uint32_t nfixed_ = 0;

    /* Slots plus maximum stack depth. */
    uint32_t nslots_ = 0;

    /* Index into the scopes array of the body scope */
    uint32_t bodyScopeIndex_ = 0;

    // Range of characters in scriptSource which contains this script's
    // source, that is, the range used by the Parser to produce this script.
    //
    // Most scripted functions have sourceStart_ == toStringStart_ and
    // sourceEnd_ == toStringEnd_. However, for functions with extra
    // qualifiers (e.g. generators, async) and for class constructors (which
    // need to return the entire class source), their values differ.
    //
    // Each field points the following locations.
    //
    //   function * f(a, b) { return a + b; }
    //   ^          ^                        ^
    //   |          |                        |
    //   |          sourceStart_             sourceEnd_
    //   |                                   |
    //   toStringStart_                      toStringEnd_
    //
    // And, in the case of class constructors, an additional toStringEnd
    // offset is used.
    //
    //   class C { constructor() { this.field = 42; } }
    //   ^         ^                                 ^ ^
    //   |         |                                 | `---------`
    //   |         sourceStart_                      sourceEnd_  |
    //   |                                                       |
    //   toStringStart_                                          toStringEnd_
    uint32_t sourceStart_ = 0;
    uint32_t sourceEnd_ = 0;
    uint32_t toStringStart_ = 0;
    uint32_t toStringEnd_ = 0;

#ifdef MOZ_VTUNE
    // Unique Method ID passed to the VTune profiler, or 0 if unset.
    // Allows attribution of different jitcode to the same source script.
    uint32_t vtuneMethodId_ = 0;
#endif

    // Number of times the script has been called or has had backedges taken.
    // When running in ion, also increased for any inlined scripts. Reset if
    // the script's JIT code is forcibly discarded.
    mozilla::Atomic<uint32_t, mozilla::Relaxed,
                    mozilla::recordreplay::Behavior::DontPreserve> warmUpCount = {};

    // 16-bit fields.

    /**
     * Number of times the |warmUpCount| was forcibly discarded. The counter is
     * reset when a script is successfully jit-compiled.
     */
    uint16_t warmUpResetCount = 0;

    /* ES6 function length. */
    uint16_t funLength_ = 0;

    /* Number of type sets used in this script for dynamic type monitoring. */
    uint16_t nTypeSets_ = 0;

    // Bit fields.

  public:
    // The kinds of the optional arrays.
    enum ArrayKind {
        CONSTS,
        OBJECTS,
        TRYNOTES,
        SCOPENOTES,
        ARRAY_KIND_BITS
    };

  private:
    struct BitFields
    {
        /*
         * Bit-fields can't have member initializers til C++2a, i.e. probably
         * C++20, so we can't initialize these to zero in place.  Instead we
         * braced-init this to all zeroes in the JSScript constructor, then
         * custom-assign particular bit-fields in the constructor body.
         */

        // The bits in this field indicate the presence/non-presence of several
        // optional arrays in |data|.  See the comments above Create() for details.
        uint8_t hasArrayBits_ : ARRAY_KIND_BITS;

        /*
         * All remaining bit-fields are single-bit bools.
         */

        // No need for result value of last expression statement.
        bool noScriptRval_ : 1;

        // Code is in strict mode.
        bool strict_ : 1;

        // Code has "use strict"; explicitly.
        bool explicitUseStrict_ : 1;

        // True if the script has a non-syntactic scope on its dynamic scope chain.
        // That is, there are objects about which we know nothing between the
        // outermost syntactic scope and the global.
        bool hasNonSyntacticScope_ : 1;

        // see Parser::selfHostingMode.
        bool selfHosted_ : 1;

        // See FunctionBox.
        bool bindingsAccessedDynamically_ : 1;
        bool funHasExtensibleScope_ : 1;

        // True if any formalIsAliased(i).
        bool funHasAnyAliasedFormal_ : 1;

        // Have warned about uses of undefined properties in this script.
        bool warnedAboutUndefinedProp_ : 1;

        // Script has singleton objects.
        bool hasSingletons_ : 1;

        // Script is a lambda to treat as running once or a global or eval script
        // that will only run once.  Which one it is can be disambiguated by
        // checking whether function() is null.
        bool treatAsRunOnce_ : 1;

        // If treatAsRunOnce, whether script has executed.
        bool hasRunOnce_ : 1;

        // Script has been reused for a clone.
        bool hasBeenCloned_ : 1;

        // Script came from eval(), and is still active.
        bool isActiveEval_ : 1;

        // Script came from eval(), and is in eval cache.
        bool isCachedEval_ : 1;

        // 'this', 'arguments' and f.apply() are used. This is likely to be a wrapper.
        bool isLikelyConstructorWrapper_ : 1;

        // IonMonkey compilation hints.

        /* Script has had hoisted bounds checks fail. */
        bool failedBoundsCheck_ : 1;

        /* Script has had hoisted shape guard fail. */
        bool failedShapeGuard_ : 1;

        bool hadFrequentBailouts_ : 1;
        bool hadOverflowBailout_ : 1;

        /* Explicitly marked as uninlineable. */
        bool uninlineable_ : 1;

        // Idempotent cache has triggered invalidation.
        bool invalidatedIdempotentCache_ : 1;

        // Lexical check did fail and bail out.
        bool failedLexicalCheck_ : 1;

        // Script has an entry in Realm::scriptCountsMap.
        bool hasScriptCounts_ : 1;

        // Script has an entry in Realm::debugScriptMap.
        bool hasDebugScript_ : 1;

        // Freeze constraints for stack type sets have been generated.
        bool hasFreezeConstraints_ : 1;

        /* See comments below. */
        bool argsHasVarBinding_ : 1;
        bool needsArgsAnalysis_ : 1;
        bool needsArgsObj_ : 1;
        bool functionHasThisBinding_ : 1;
        bool functionHasExtraBodyVarScope_ : 1;

        // Whether the arguments object for this script, if it needs one, should be
        // mapped (alias formal parameters).
        bool hasMappedArgsObj_ : 1;

        // Generation for this script's TypeScript. If out of sync with the
        // TypeZone's generation, the TypeScript needs to be swept.
        //
        // This should be a uint32 but is instead a bool so that MSVC packs it
        // correctly.
        bool typesGeneration_ : 1;

        // Do not relazify this script. This is used by the relazify() testing
        // function for scripts that are on the stack and also by the AutoDelazify
        // RAII class. Usually we don't relazify functions in compartments with
        // scripts on the stack, but the relazify() testing function overrides that,
        // and sometimes we're working with a cross-compartment function and need to
        // keep it from relazifying.
        bool doNotRelazify_ : 1;

        // Script contains inner functions. Used to check if we can relazify the
        // script.
        bool hasInnerFunctions_ : 1;

        bool needsHomeObject_ : 1;

        bool isDerivedClassConstructor_ : 1;
        bool isDefaultClassConstructor_ : 1;

        // True if this function is a generator function or async generator.
        bool isGenerator_ : 1;

        // True if this function is an async function or async generator.
        bool isAsync_ : 1;

        bool hasRest_ : 1;

        // True if the debugger's onNewScript hook has not yet been called.
        bool hideScriptFromDebugger_ : 1;
    } bitFields_;

    //
    // End of fields.  Start methods.
    //

  private:
    template <js::XDRMode mode>
    friend
    js::XDRResult
    js::XDRScript(js::XDRState<mode>* xdr, js::HandleScope enclosingScope,
                  js::HandleScriptSourceObject sourceObject, js::HandleFunction fun,
                  js::MutableHandleScript scriptp);

    friend bool
    js::detail::CopyScript(JSContext* cx, js::HandleScript src, js::HandleScript dst,
                           js::MutableHandle<JS::GCVector<js::Scope*>> scopes);

  private:
    JSScript(JS::Realm* realm, uint8_t* stubEntry, const JS::ReadOnlyCompileOptions& options,
             js::HandleObject sourceObject, uint32_t bufStart, uint32_t bufEnd,
             uint32_t toStringStart, uint32_t toStringend);

    static JSScript* createInitialized(JSContext* cx, const JS::ReadOnlyCompileOptions& options,
                                       js::HandleObject sourceObject,
                                       uint32_t bufStart, uint32_t bufEnd,
                                       uint32_t toStringStart, uint32_t toStringEnd);

  public:
    static JSScript* Create(JSContext* cx,
                            const JS::ReadOnlyCompileOptions& options,
                            js::HandleObject sourceObject,
                            uint32_t sourceStart, uint32_t sourceEnd,
                            uint32_t toStringStart, uint32_t toStringEnd);

    // Three ways ways to initialize a JSScript. Callers of partiallyInit()
    // are responsible for notifying the debugger after successfully creating
    // any kind (function or other) of new JSScript.  However, callers of
    // fullyInitFromEmitter() do not need to do this.
    static bool partiallyInit(JSContext* cx, JS::Handle<JSScript*> script,
                              uint32_t nscopes, uint32_t nconsts, uint32_t nobjects,
                              uint32_t ntrynotes, uint32_t nscopenotes, uint32_t nyieldoffsets);

  private:
    static void initFromFunctionBox(js::HandleScript script, js::frontend::FunctionBox* funbox);
    static void initFromModuleContext(js::HandleScript script);

  public:
    static bool fullyInitFromEmitter(JSContext* cx, js::HandleScript script,
                                     js::frontend::BytecodeEmitter* bce);

    // Initialize the Function.prototype script.
    static bool initFunctionPrototype(JSContext* cx, js::HandleScript script,
                                      JS::HandleFunction functionProto);

#ifdef DEBUG
  private:
    // Assert that jump targets are within the code array of the script.
    void assertValidJumpTargets() const;
#endif

  public:
    inline JSPrincipals* principals();

    JS::Compartment* compartment() const { return JS::GetCompartmentForRealm(realm_); }
    JS::Compartment* maybeCompartment() const { return compartment(); }
    JS::Realm* realm() const { return realm_; }

    js::SharedScriptData* scriptData() {
        return scriptData_;
    }

    // Script bytecode is immutable after creation.
    jsbytecode* code() const {
        if (!scriptData_) {
            return nullptr;
        }
        return scriptData_->code();
    }
    bool isUncompleted() const {
        // code() becomes non-null only if this script is complete.
        // See the comment in JSScript::fullyInitFromEmitter.
        return !code();
    }

    size_t length() const {
        MOZ_ASSERT(scriptData_);
        return scriptData_->codeLength();
    }

    jsbytecode* codeEnd() const { return code() + length(); }

    jsbytecode* lastPC() const {
        jsbytecode* pc = codeEnd() - js::JSOP_RETRVAL_LENGTH;
        MOZ_ASSERT(*pc == JSOP_RETRVAL);
        return pc;
    }

    bool containsPC(const jsbytecode* pc) const {
        return pc >= code() && pc < codeEnd();
    }

    size_t pcToOffset(const jsbytecode* pc) const {
        MOZ_ASSERT(containsPC(pc));
        return size_t(pc - code());
    }

    jsbytecode* offsetToPC(size_t offset) const {
        MOZ_ASSERT(offset < length());
        return code() + offset;
    }

    size_t mainOffset() const {
        return mainOffset_;
    }

    uint32_t lineno() const {
        return lineno_;
    }

    uint32_t column() const {
        return column_;
    }

    void setColumn(size_t column) { column_ = column; }

    // The fixed part of a stack frame is comprised of vars (in function and
    // module code) and block-scoped locals (in all kinds of code).
    size_t nfixed() const {
        return nfixed_;
    }

    // Number of fixed slots reserved for slots that are always live. Only
    // nonzero for function or module code.
    size_t numAlwaysLiveFixedSlots() const {
        if (bodyScope()->is<js::FunctionScope>()) {
            return bodyScope()->as<js::FunctionScope>().nextFrameSlot();
        }
        if (bodyScope()->is<js::ModuleScope>()) {
            return bodyScope()->as<js::ModuleScope>().nextFrameSlot();
        }
        return 0;
    }

    // Calculate the number of fixed slots that are live at a particular bytecode.
    size_t calculateLiveFixed(jsbytecode* pc);

    size_t nslots() const {
        return nslots_;
    }

    unsigned numArgs() const {
        if (bodyScope()->is<js::FunctionScope>()) {
            return bodyScope()->as<js::FunctionScope>().numPositionalFormalParameters();
        }
        return 0;
    }

    inline js::Shape* initialEnvironmentShape() const;

    bool functionHasParameterExprs() const {
        // Only functions have parameters.
        js::Scope* scope = bodyScope();
        if (!scope->is<js::FunctionScope>()) {
            return false;
        }
        return scope->as<js::FunctionScope>().hasParameterExprs();
    }

    size_t nTypeSets() const {
        return nTypeSets_;
    }

    size_t funLength() const {
        return funLength_;
    }

    static size_t offsetOfFunLength() {
        return offsetof(JSScript, funLength_);
    }

    uint32_t sourceStart() const {
        return sourceStart_;
    }

    uint32_t sourceEnd() const {
        return sourceEnd_;
    }

    uint32_t sourceLength() const {
        return sourceEnd_ - sourceStart_;
    }

    uint32_t toStringStart() const {
        return toStringStart_;
    }

    uint32_t toStringEnd() const {
        return toStringEnd_;
    }

    bool noScriptRval() const {
        return bitFields_.noScriptRval_;
    }

    bool strict() const {
        return bitFields_.strict_;
    }

    bool explicitUseStrict() const { return bitFields_.explicitUseStrict_; }

    bool hasNonSyntacticScope() const {
        return bitFields_.hasNonSyntacticScope_;
    }

    bool selfHosted() const { return bitFields_.selfHosted_; }
    bool bindingsAccessedDynamically() const { return bitFields_.bindingsAccessedDynamically_; }
    bool funHasExtensibleScope() const {
        return bitFields_.funHasExtensibleScope_;
    }
    bool funHasAnyAliasedFormal() const {
        return bitFields_.funHasAnyAliasedFormal_;
    }

    bool hasSingletons() const { return bitFields_.hasSingletons_; }
    bool treatAsRunOnce() const {
        return bitFields_.treatAsRunOnce_;
    }
    bool hasRunOnce() const { return bitFields_.hasRunOnce_; }
    bool hasBeenCloned() const { return bitFields_.hasBeenCloned_; }

    void setTreatAsRunOnce() { bitFields_.treatAsRunOnce_ = true; }
    void setHasRunOnce() { bitFields_.hasRunOnce_ = true; }
    void setHasBeenCloned() { bitFields_.hasBeenCloned_ = true; }

    bool isActiveEval() const { return bitFields_.isActiveEval_; }
    bool isCachedEval() const { return bitFields_.isCachedEval_; }

    void cacheForEval() {
        MOZ_ASSERT(isActiveEval());
        MOZ_ASSERT(!isCachedEval());
        bitFields_.isActiveEval_ = false;
        bitFields_.isCachedEval_ = true;
        // IsEvalCacheCandidate will make sure that there's nothing in this
        // script that would prevent reexecution even if isRunOnce is
        // true.  So just pretend like we never ran this script.
        bitFields_.hasRunOnce_ = false;
    }

    void uncacheForEval() {
        MOZ_ASSERT(isCachedEval());
        MOZ_ASSERT(!isActiveEval());
        bitFields_.isCachedEval_ = false;
        bitFields_.isActiveEval_ = true;
    }

    void setActiveEval() { bitFields_.isActiveEval_ = true; }

    bool isLikelyConstructorWrapper() const {
        return bitFields_.isLikelyConstructorWrapper_;
    }
    void setLikelyConstructorWrapper() { bitFields_.isLikelyConstructorWrapper_ = true; }

    bool failedBoundsCheck() const {
        return bitFields_.failedBoundsCheck_;
    }
    bool failedShapeGuard() const {
        return bitFields_.failedShapeGuard_;
    }
    bool hadFrequentBailouts() const {
        return bitFields_.hadFrequentBailouts_;
    }
    bool hadOverflowBailout() const {
        return bitFields_.hadOverflowBailout_;
    }
    bool uninlineable() const {
        return bitFields_.uninlineable_;
    }
    bool invalidatedIdempotentCache() const {
        return bitFields_.invalidatedIdempotentCache_;
    }
    bool failedLexicalCheck() const {
        return bitFields_.failedLexicalCheck_;
    }
    bool isDefaultClassConstructor() const {
        return bitFields_.isDefaultClassConstructor_;
    }

    void setFailedBoundsCheck() { bitFields_.failedBoundsCheck_ = true; }
    void setFailedShapeGuard() { bitFields_.failedShapeGuard_ = true; }
    void setHadFrequentBailouts() { bitFields_.hadFrequentBailouts_ = true; }
    void setHadOverflowBailout() { bitFields_.hadOverflowBailout_ = true; }
    void setUninlineable() { bitFields_.uninlineable_ = true; }
    void setInvalidatedIdempotentCache() { bitFields_.invalidatedIdempotentCache_ = true; }
    void setFailedLexicalCheck() { bitFields_.failedLexicalCheck_ = true; }
    void setIsDefaultClassConstructor() { bitFields_.isDefaultClassConstructor_ = true; }

    bool hasScriptCounts() const { return bitFields_.hasScriptCounts_; }
    bool hasScriptName();

    bool hasFreezeConstraints() const { return bitFields_.hasFreezeConstraints_; }
    void setHasFreezeConstraints() { bitFields_.hasFreezeConstraints_ = true; }

    bool warnedAboutUndefinedProp() const { return bitFields_.warnedAboutUndefinedProp_; }
    void setWarnedAboutUndefinedProp() { bitFields_.warnedAboutUndefinedProp_ = true; }

    /* See ContextFlags::funArgumentsHasLocalBinding comment. */
    bool argumentsHasVarBinding() const {
        return bitFields_.argsHasVarBinding_;
    }
    void setArgumentsHasVarBinding();
    bool argumentsAliasesFormals() const {
        return argumentsHasVarBinding() && hasMappedArgsObj();
    }

    js::GeneratorKind generatorKind() const {
        return bitFields_.isGenerator_ ? js::GeneratorKind::Generator : js::GeneratorKind::NotGenerator;
    }
    bool isGenerator() const { return bitFields_.isGenerator_; }
    void setGeneratorKind(js::GeneratorKind kind) {
        // A script only gets its generator kind set as part of initialization,
        // so it can only transition from not being a generator.
        MOZ_ASSERT(!isGenerator());
        bitFields_.isGenerator_ = kind == js::GeneratorKind::Generator;
    }

    js::FunctionAsyncKind asyncKind() const {
        return bitFields_.isAsync_
               ? js::FunctionAsyncKind::AsyncFunction
               : js::FunctionAsyncKind::SyncFunction;
    }
    bool isAsync() const {
        return bitFields_.isAsync_;
    }

    void setAsyncKind(js::FunctionAsyncKind kind) {
        bitFields_.isAsync_ = kind == js::FunctionAsyncKind::AsyncFunction;
    }

    bool hasRest() const {
        return bitFields_.hasRest_;
    }
    void setHasRest() {
        bitFields_.hasRest_ = true;
    }

    bool hideScriptFromDebugger() const {
        return bitFields_.hideScriptFromDebugger_;
    }
    void clearHideScriptFromDebugger() {
        bitFields_.hideScriptFromDebugger_ = false;
    }

    void setNeedsHomeObject() {
        bitFields_.needsHomeObject_ = true;
    }
    bool needsHomeObject() const {
        return bitFields_.needsHomeObject_;
    }

    bool isDerivedClassConstructor() const {
        return bitFields_.isDerivedClassConstructor_;
    }

    /*
     * As an optimization, even when argsHasLocalBinding, the function prologue
     * may not need to create an arguments object. This is determined by
     * needsArgsObj which is set by AnalyzeArgumentsUsage. When !needsArgsObj,
     * the prologue may simply write MagicValue(JS_OPTIMIZED_ARGUMENTS) to
     * 'arguments's slot and any uses of 'arguments' will be guaranteed to
     * handle this magic value. To avoid spurious arguments object creation, we
     * maintain the invariant that needsArgsObj is only called after the script
     * has been analyzed.
     */
    bool analyzedArgsUsage() const { return !bitFields_.needsArgsAnalysis_; }
    inline bool ensureHasAnalyzedArgsUsage(JSContext* cx);
    bool needsArgsObj() const {
        MOZ_ASSERT(analyzedArgsUsage());
        return bitFields_.needsArgsObj_;
    }
    void setNeedsArgsObj(bool needsArgsObj);
    static bool argumentsOptimizationFailed(JSContext* cx, js::HandleScript script);

    bool hasMappedArgsObj() const {
        return bitFields_.hasMappedArgsObj_;
    }

    bool functionHasThisBinding() const {
        return bitFields_.functionHasThisBinding_;
    }

    /*
     * Arguments access (via JSOP_*ARG* opcodes) must access the canonical
     * location for the argument. If an arguments object exists AND it's mapped
     * ('arguments' aliases formals), then all access must go through the
     * arguments object. Otherwise, the local slot is the canonical location for
     * the arguments. Note: if a formal is aliased through the scope chain, then
     * script->formalIsAliased and JSOP_*ARG* opcodes won't be emitted at all.
     */
    bool argsObjAliasesFormals() const {
        return needsArgsObj() && hasMappedArgsObj();
    }

    uint32_t typesGeneration() const {
        return (uint32_t) bitFields_.typesGeneration_;
    }

    void setTypesGeneration(uint32_t generation) {
        MOZ_ASSERT(generation <= 1);
        bitFields_.typesGeneration_ = (bool) generation;
    }

    void setDoNotRelazify(bool b) {
        bitFields_.doNotRelazify_ = b;
    }

    void setHasInnerFunctions(bool b) {
        bitFields_.hasInnerFunctions_ = b;
    }

    bool hasInnerFunctions() const {
        return bitFields_.hasInnerFunctions_;
    }

    bool hasAnyIonScript() const {
        return hasIonScript();
    }

    bool hasIonScript() const {
        bool res = ion && ion != ION_DISABLED_SCRIPT && ion != ION_COMPILING_SCRIPT &&
                          ion != ION_PENDING_SCRIPT;
        MOZ_ASSERT_IF(res, baseline);
        return res;
    }
    bool canIonCompile() const {
        return ion != ION_DISABLED_SCRIPT;
    }
    bool isIonCompilingOffThread() const {
        return ion == ION_COMPILING_SCRIPT;
    }

    js::jit::IonScript* ionScript() const {
        MOZ_ASSERT(hasIonScript());
        return ion;
    }
    js::jit::IonScript* maybeIonScript() const {
        return ion;
    }
    js::jit::IonScript* const* addressOfIonScript() const {
        return &ion;
    }
    void setIonScript(JSRuntime* rt, js::jit::IonScript* ionScript);

    bool hasBaselineScript() const {
        bool res = baseline && baseline != BASELINE_DISABLED_SCRIPT;
        MOZ_ASSERT_IF(!res, !ion || ion == ION_DISABLED_SCRIPT);
        return res;
    }
    bool canBaselineCompile() const {
        return baseline != BASELINE_DISABLED_SCRIPT;
    }
    js::jit::BaselineScript* baselineScript() const {
        MOZ_ASSERT(hasBaselineScript());
        return baseline;
    }
    inline void setBaselineScript(JSRuntime* rt, js::jit::BaselineScript* baselineScript);

    void updateJitCodeRaw(JSRuntime* rt);

    static size_t offsetOfBaselineScript() {
        return offsetof(JSScript, baseline);
    }
    static size_t offsetOfIonScript() {
        return offsetof(JSScript, ion);
    }
    static constexpr size_t offsetOfJitCodeRaw() {
        return offsetof(JSScript, jitCodeRaw_);
    }
    static constexpr size_t offsetOfJitCodeSkipArgCheck() {
        return offsetof(JSScript, jitCodeSkipArgCheck_);
    }
    uint8_t* jitCodeRaw() const {
        return jitCodeRaw_;
    }

    bool isRelazifiable() const {
        return (selfHosted() || lazyScript) && !bitFields_.hasInnerFunctions_ && !types_ &&
               !isGenerator() && !isAsync() &&
               !isDefaultClassConstructor() &&
               !hasBaselineScript() && !hasAnyIonScript() &&
               !bitFields_.doNotRelazify_;
    }
    void setLazyScript(js::LazyScript* lazy) {
        lazyScript = lazy;
    }
    js::LazyScript* maybeLazyScript() {
        return lazyScript;
    }

    /*
     * Original compiled function for the script, if it has a function.
     * nullptr for global and eval scripts.
     * The delazifying variant ensures that the function isn't lazy. The
     * non-delazifying variant must only be used after earlier code has
     * called ensureNonLazyCanonicalFunction and while the function can't
     * have been relazified.
     */
    inline JSFunction* functionDelazifying() const;
    JSFunction* functionNonDelazifying() const {
        if (bodyScope()->is<js::FunctionScope>()) {
            return bodyScope()->as<js::FunctionScope>().canonicalFunction();
        }
        return nullptr;
    }
    /*
     * De-lazifies the canonical function. Must be called before entering code
     * that expects the function to be non-lazy.
     */
    inline void ensureNonLazyCanonicalFunction();

    bool isModule() const { return bodyScope()->is<js::ModuleScope>(); }
    js::ModuleObject* module() const {
        if (isModule()) {
            return bodyScope()->as<js::ModuleScope>().module();
        }
        return nullptr;
    }

    bool isGlobalOrEvalCode() const {
        return bodyScope()->is<js::GlobalScope>() || bodyScope()->is<js::EvalScope>();
    }
    bool isGlobalCode() const {
        return bodyScope()->is<js::GlobalScope>();
    }

    // Returns true if the script may read formal arguments on the stack
    // directly, via lazy arguments or a rest parameter.
    bool mayReadFrameArgsDirectly();

    static JSFlatString* sourceData(JSContext* cx, JS::HandleScript script);

    MOZ_MUST_USE bool appendSourceDataForToString(JSContext* cx, js::StringBuffer& buf);

    static bool loadSource(JSContext* cx, js::ScriptSource* ss, bool* worked);

    void setSourceObject(JSObject* object);
    JSObject* sourceObject() const {
        return sourceObject_;
    }
    js::ScriptSourceObject& scriptSourceUnwrap() const;
    js::ScriptSource* scriptSource() const;
    js::ScriptSource* maybeForwardedScriptSource() const;

    void setDefaultClassConstructorSpan(JSObject* sourceObject, uint32_t start, uint32_t end,
                                        unsigned line, unsigned column);

    bool mutedErrors() const { return scriptSource()->mutedErrors(); }
    const char* filename() const { return scriptSource()->filename(); }
    const char* maybeForwardedFilename() const { return maybeForwardedScriptSource()->filename(); }

#ifdef MOZ_VTUNE
    uint32_t vtuneMethodID() const { return vtuneMethodId_; }
#endif

  public:

    /* Return whether this script was compiled for 'eval' */
    bool isForEval() const {
        MOZ_ASSERT_IF(isCachedEval() || isActiveEval(), bodyScope()->is<js::EvalScope>());
        return isCachedEval() || isActiveEval();
    }

    /* Return whether this is a 'direct eval' script in a function scope. */
    bool isDirectEvalInFunction() const {
        if (!isForEval()) {
            return false;
        }
        return bodyScope()->hasOnChain(js::ScopeKind::Function);
    }

    /*
     * Return whether this script is a top-level script.
     *
     * If we evaluate some code which contains a syntax error, then we might
     * produce a JSScript which has no associated bytecode. Testing with
     * |code()| filters out this kind of scripts.
     *
     * If this script has a function associated to it, then it is not the
     * top-level of a file.
     */
    bool isTopLevel() { return code() && !functionNonDelazifying(); }

    /* Ensure the script has a TypeScript. */
    inline bool ensureHasTypes(JSContext* cx, js::AutoKeepTypeScripts&);

    inline js::TypeScript* types(const js::AutoSweepTypeScript& sweep);

    inline bool typesNeedsSweep() const;

    void sweepTypes(const js::AutoSweepTypeScript& sweep);

    inline js::GlobalObject& global() const;
    js::GlobalObject& uninlinedGlobal() const;

    uint32_t bodyScopeIndex() const {
        return bodyScopeIndex_;
    }

    js::Scope* bodyScope() const {
        return getScope(bodyScopeIndex_);
    }

    js::Scope* outermostScope() const {
        // The body scope may not be the outermost scope in the script when
        // the decl env scope is present.
        size_t index = 0;
        return getScope(index);
    }

    bool functionHasExtraBodyVarScope() const {
        MOZ_ASSERT_IF(bitFields_.functionHasExtraBodyVarScope_, functionHasParameterExprs());
        return bitFields_.functionHasExtraBodyVarScope_;
    }

    js::VarScope* functionExtraBodyVarScope() const {
        MOZ_ASSERT(functionHasExtraBodyVarScope());
        for (js::Scope* scope : scopes()) {
            if (scope->kind() == js::ScopeKind::FunctionBodyVar) {
                return &scope->as<js::VarScope>();
            }
        }
        MOZ_CRASH("Function extra body var scope not found");
    }

    bool needsBodyEnvironment() const {
        for (js::Scope* scope : scopes()) {
            if (ScopeKindIsInBody(scope->kind()) && scope->hasEnvironment()) {
                return true;
            }
        }
        return false;
    }

    inline js::LexicalScope* maybeNamedLambdaScope() const;

    js::Scope* enclosingScope() const {
        return outermostScope()->enclosing();
    }

  private:
    bool makeTypes(JSContext* cx);

    bool createScriptData(JSContext* cx, uint32_t codeLength, uint32_t srcnotesLength,
                          uint32_t natoms);
    bool shareScriptData(JSContext* cx);
    void freeScriptData();
    void setScriptData(js::SharedScriptData* data);

  public:
    uint32_t getWarmUpCount() const { return warmUpCount; }
    uint32_t incWarmUpCounter(uint32_t amount = 1) { return warmUpCount += amount; }
    uint32_t* addressOfWarmUpCounter() { return reinterpret_cast<uint32_t*>(&warmUpCount); }
    static size_t offsetOfWarmUpCounter() { return offsetof(JSScript, warmUpCount); }
    void resetWarmUpCounter() { incWarmUpResetCounter(); warmUpCount = 0; }

    uint16_t getWarmUpResetCount() const { return warmUpResetCount; }
    uint16_t incWarmUpResetCounter(uint16_t amount = 1) { return warmUpResetCount += amount; }
    void resetWarmUpResetCounter() { warmUpResetCount = 0; }

  public:
    bool initScriptCounts(JSContext* cx);
    bool initScriptName(JSContext* cx);
    js::ScriptCounts& getScriptCounts();
    const char* getScriptName();
    js::PCCounts* maybeGetPCCounts(jsbytecode* pc);
    const js::PCCounts* maybeGetThrowCounts(jsbytecode* pc);
    js::PCCounts* getThrowCounts(jsbytecode* pc);
    uint64_t getHitCount(jsbytecode* pc);
    void incHitCount(jsbytecode* pc); // Used when we bailout out of Ion.
    void addIonCounts(js::jit::IonScriptCounts* ionCounts);
    js::jit::IonScriptCounts* getIonCounts();
    void releaseScriptCounts(js::ScriptCounts* counts);
    void destroyScriptCounts();
    void destroyScriptName();
    void clearHasScriptCounts();

    jsbytecode* main() const {
        return code() + mainOffset();
    }

    /*
     * computedSizeOfData() is the in-use size of all the data sections.
     * sizeOfData() is the size of the block allocated to hold all the data
     * sections (which can be larger than the in-use size).
     */
    size_t computedSizeOfData() const;
    size_t sizeOfData(mozilla::MallocSizeOf mallocSizeOf) const;
    size_t sizeOfTypeScript(mozilla::MallocSizeOf mallocSizeOf) const;

    bool hasArray(ArrayKind kind) const {
        return bitFields_.hasArrayBits_ & (1 << kind);
    }
    void setHasArray(ArrayKind kind) { bitFields_.hasArrayBits_ |= (1 << kind); }
    void cloneHasArray(JSScript* script) {
        bitFields_.hasArrayBits_ = script->bitFields_.hasArrayBits_;
    }

    bool hasConsts() const       { return hasArray(CONSTS); }
    bool hasObjects() const      { return hasArray(OBJECTS); }
    bool hasTrynotes() const     { return hasArray(TRYNOTES); }
    bool hasScopeNotes() const   { return hasArray(SCOPENOTES); }
    bool hasYieldAndAwaitOffsets() const {
        return isGenerator() || isAsync();
    }

#define OFF(fooOff, hasFoo, t)   (fooOff() + (hasFoo() ? sizeof(t) : 0))

    size_t scopesOffset() const       { return 0; }
    size_t constsOffset() const       { return scopesOffset() + sizeof(js::ScopeArray); }
    size_t objectsOffset() const      { return OFF(constsOffset,     hasConsts,     js::ConstArray); }
    size_t trynotesOffset() const     { return OFF(objectsOffset,    hasObjects,    js::ObjectArray); }
    size_t scopeNotesOffset() const   { return OFF(trynotesOffset,   hasTrynotes,   js::TryNoteArray); }
    size_t yieldAndAwaitOffsetsOffset() const {
        return OFF(scopeNotesOffset, hasScopeNotes, js::ScopeNoteArray);
    }

#undef OFF

    size_t dataSize() const { return dataSize_; }

  private:

    js::ConstArray* constsRaw() const {
        MOZ_ASSERT(hasConsts());
        return reinterpret_cast<js::ConstArray*>(data + constsOffset());
    }

    js::ObjectArray* objectsRaw() const {
        MOZ_ASSERT(hasObjects());
        return reinterpret_cast<js::ObjectArray*>(data + objectsOffset());
    }

    js::ScopeArray* scopesRaw() const {
        return reinterpret_cast<js::ScopeArray*>(data + scopesOffset());
    }

    js::TryNoteArray* trynotesRaw() const {
        MOZ_ASSERT(hasTrynotes());
        return reinterpret_cast<js::TryNoteArray*>(data + trynotesOffset());
    }

    js::ScopeNoteArray* scopeNotesRaw() const {
        MOZ_ASSERT(hasScopeNotes());
        return reinterpret_cast<js::ScopeNoteArray*>(data + scopeNotesOffset());
    }

    js::YieldAndAwaitOffsetArray& yieldAndAwaitOffsetsRaw() const {
        MOZ_ASSERT(hasYieldAndAwaitOffsets());
        return *reinterpret_cast<js::YieldAndAwaitOffsetArray*>(data +
                                                                yieldAndAwaitOffsetsOffset());
    }

  public:

    mozilla::Span<js::GCPtrValue> consts() const {
        js::ConstArray* array = constsRaw();
        return mozilla::MakeSpan(array->vector, array->length);
    }

    mozilla::Span<js::GCPtrObject> objects() const {
        js::ObjectArray* array = objectsRaw();
        return mozilla::MakeSpan(array->vector, array->length);
    }

    mozilla::Span<js::GCPtrScope> scopes() const {
        js::ScopeArray* array = scopesRaw();
        return mozilla::MakeSpan(array->vector, array->length);
    }

    mozilla::Span<JSTryNote> trynotes() const {
        js::TryNoteArray* array = trynotesRaw();
        return mozilla::MakeSpan(array->vector, array->length);
    }

    mozilla::Span<js::ScopeNote> scopeNotes() const {
        js::ScopeNoteArray* array = scopeNotesRaw();
        return mozilla::MakeSpan(array->vector, array->length);
    }

    mozilla::Span<uint32_t> yieldAndAwaitOffsets() const {
        js::YieldAndAwaitOffsetArray& array = yieldAndAwaitOffsetsRaw();
        return mozilla::MakeSpan(&array[0], array.length());
    }

    bool hasLoops();

    uint32_t numNotes() const {
        MOZ_ASSERT(scriptData_);
        return scriptData_->numNotes();
    }
    jssrcnote* notes() const {
        MOZ_ASSERT(scriptData_);
        return scriptData_->notes();
    }

    size_t natoms() const {
        MOZ_ASSERT(scriptData_);
        return scriptData_->natoms();
    }
    js::GCPtrAtom* atoms() const {
        MOZ_ASSERT(scriptData_);
        return scriptData_->atoms();
    }

    js::GCPtrAtom& getAtom(size_t index) const {
        MOZ_ASSERT(index < natoms());
        return atoms()[index];
    }

    js::GCPtrAtom& getAtom(jsbytecode* pc) const {
        MOZ_ASSERT(containsPC(pc) && containsPC(pc + sizeof(uint32_t)));
        MOZ_ASSERT(js::JOF_OPTYPE((JSOp)*pc) == JOF_ATOM);
        return getAtom(GET_UINT32_INDEX(pc));
    }

    js::PropertyName* getName(size_t index) {
        return getAtom(index)->asPropertyName();
    }

    js::PropertyName* getName(jsbytecode* pc) const {
        return getAtom(pc)->asPropertyName();
    }

    JSObject* getObject(size_t index) {
        MOZ_ASSERT(objects()[index]->isTenured());
        return objects()[index];
    }

    JSObject* getObject(jsbytecode* pc) {
        MOZ_ASSERT(containsPC(pc) && containsPC(pc + sizeof(uint32_t)));
        return getObject(GET_UINT32_INDEX(pc));
    }

    js::Scope* getScope(size_t index) const {
        return scopes()[index];
    }

    js::Scope* getScope(jsbytecode* pc) const {
        // This method is used to get a scope directly using a JSOp with an
        // index. To search through ScopeNotes to look for a Scope using pc,
        // use lookupScope.
        MOZ_ASSERT(containsPC(pc) && containsPC(pc + sizeof(uint32_t)));
        MOZ_ASSERT(js::JOF_OPTYPE(JSOp(*pc)) == JOF_SCOPE,
                   "Did you mean to use lookupScope(pc)?");
        return getScope(GET_UINT32_INDEX(pc));
    }

    inline JSFunction* getFunction(size_t index);
    JSFunction* function() const {
        if (functionNonDelazifying()) {
            return functionNonDelazifying();
        }
        return nullptr;
    }

    inline js::RegExpObject* getRegExp(size_t index);
    inline js::RegExpObject* getRegExp(jsbytecode* pc);

    const js::Value& getConst(size_t index) {
        return consts()[index];
    }

    // The following 3 functions find the static scope just before the
    // execution of the instruction pointed to by pc.

    js::Scope* lookupScope(jsbytecode* pc);

    js::Scope* innermostScope(jsbytecode* pc);
    js::Scope* innermostScope() { return innermostScope(main()); }

    /*
     * The isEmpty method tells whether this script has code that computes any
     * result (not return value, result AKA normal completion value) other than
     * JSVAL_VOID, or any other effects.
     */
    bool isEmpty() const {
        if (length() > 3) {
            return false;
        }

        jsbytecode* pc = code();
        if (noScriptRval() && JSOp(*pc) == JSOP_FALSE) {
            ++pc;
        }
        return JSOp(*pc) == JSOP_RETRVAL;
    }

    bool formalIsAliased(unsigned argSlot);
    bool formalLivesInArgumentsObject(unsigned argSlot);

  private:
    /* Change this->stepMode to |newValue|. */
    void setNewStepMode(js::FreeOp* fop, uint32_t newValue);

    bool ensureHasDebugScript(JSContext* cx);
    js::DebugScript* debugScript();
    js::DebugScript* releaseDebugScript();
    void destroyDebugScript(js::FreeOp* fop);

  public:
    bool hasBreakpointsAt(jsbytecode* pc);
    bool hasAnyBreakpointsOrStepMode() { return bitFields_.hasDebugScript_; }

    // See comment above 'debugMode' in Realm.h for explanation of
    // invariants of debuggee compartments, scripts, and frames.
    inline bool isDebuggee() const;

    js::BreakpointSite* getBreakpointSite(jsbytecode* pc)
    {
        return bitFields_.hasDebugScript_ ? debugScript()->breakpoints[pcToOffset(pc)] : nullptr;
    }

    js::BreakpointSite* getOrCreateBreakpointSite(JSContext* cx, jsbytecode* pc);

    void destroyBreakpointSite(js::FreeOp* fop, jsbytecode* pc);

    void clearBreakpointsIn(js::FreeOp* fop, js::Debugger* dbg, JSObject* handler);

    /*
     * Increment or decrement the single-step count. If the count is non-zero
     * then the script is in single-step mode.
     *
     * Only incrementing is fallible, as it could allocate a DebugScript.
     */
    bool incrementStepModeCount(JSContext* cx);
    void decrementStepModeCount(js::FreeOp* fop);

    bool stepModeEnabled() { return bitFields_.hasDebugScript_ && !!debugScript()->stepMode; }

#ifdef DEBUG
    uint32_t stepModeCount() { return bitFields_.hasDebugScript_ ? debugScript()->stepMode : 0; }
#endif

    void finalize(js::FreeOp* fop);

    static const JS::TraceKind TraceKind = JS::TraceKind::Script;

    void traceChildren(JSTracer* trc);

    // A helper class to prevent relazification of the given function's script
    // while it's holding on to it.  This class automatically roots the script.
    class AutoDelazify;
    friend class AutoDelazify;

    class AutoDelazify
    {
        JS::RootedScript script_;
        JSContext* cx_;
        bool oldDoNotRelazify_;
      public:
        explicit AutoDelazify(JSContext* cx, JS::HandleFunction fun = nullptr)
            : script_(cx)
            , cx_(cx)
            , oldDoNotRelazify_(false)
        {
            holdScript(fun);
        }

        ~AutoDelazify()
        {
            dropScript();
        }

        void operator=(JS::HandleFunction fun)
        {
            dropScript();
            holdScript(fun);
        }

        operator JS::HandleScript() const { return script_; }
        explicit operator bool() const { return script_; }

      private:
        void holdScript(JS::HandleFunction fun);
        void dropScript();
    };

    // Return whether the record/replay execution progress counter
    // (see RecordReplay.h) should be updated as this script runs.
    inline bool trackRecordReplayProgress() const;
};

/* If this fails, add/remove padding within JSScript. */
static_assert(sizeof(JSScript) % js::gc::CellAlignBytes == 0,
              "Size of JSScript must be an integral multiple of js::gc::CellAlignBytes");

namespace js {

// Information about a script which may be (or has been) lazily compiled to
// bytecode from its source.
class LazyScript : public gc::TenuredCell
{
  private:
    // If non-nullptr, the script has been compiled and this is a forwarding
    // pointer to the result. This is a weak pointer: after relazification, we
    // can collect the script if there are no other pointers to it.
    WeakRef<JSScript*> script_;

    // Original function with which the lazy script is associated.
    GCPtrFunction function_;

    // This field holds one of:
    //   * LazyScript in which the script is nested.  This case happens if the
    //     enclosing script is lazily parsed and have never been compiled.
    //
    //     This is used by the debugger to delazify the enclosing scripts
    //     recursively.  The all ancestor LazyScripts in this linked-list are
    //     kept alive as long as this LazyScript is alive, which doesn't result
    //     in keeping them unnecessarily alive outside of the debugger for the
    //     following reasons:
    //
    //       * Outside of the debugger, a LazyScript is visible to user (which
    //         means the LazyScript can be pointed from somewhere else than the
    //         enclosing script) only if the enclosing script is compiled and
    //         executed.  While compiling the enclosing script, this field is
    //         changed to point the enclosing scope.  So the enclosing
    //         LazyScript is no more in the list.
    //       * Before the enclosing script gets compiled, this LazyScript is
    //         kept alive only if the outermost LazyScript in the list is kept
    //         alive.
    //       * Once this field is changed to point the enclosing scope, this
    //         field will never point the enclosing LazyScript again, since
    //         relazification is not performed on non-leaf scripts.
    //
    //   * Scope in which the script is nested.  This case happens if the
    //     enclosing script has ever been compiled.
    //
    //   * nullptr for incomplete (initial or failure) state
    //
    // This field should be accessed via accessors:
    //   * enclosingScope
    //   * setEnclosingScope (cannot be called twice)
    //   * enclosingLazyScript
    //   * setEnclosingLazyScript (cannot be called twice)
    // after checking:
    //   * hasEnclosingLazyScript
    //   * hasEnclosingScope
    //
    // The transition of fields are following:
    //
    //  o                               o
    //  | when function is lazily       | when decoded from XDR,
    //  | parsed inside a function      | and enclosing script is lazy
    //  | which is lazily parsed        | (CreateForXDR without enclosingScope)
    //  | (Create)                      |
    //  v                               v
    // +---------+                     +---------+
    // | nullptr |                     | nullptr |
    // +---------+                     +---------+
    //  |                               |
    //  | when enclosing function is    | when enclosing script is decoded
    //  | lazily parsed and this        | and this script's function is put
    //  | script's function is put      | into innerFunctions()
    //  | into innerFunctions()         | (setEnclosingLazyScript)
    //  | (setEnclosingLazyScript)      |
    //  |                               |
    //  |                               |     o
    //  |                               |     | when function is lazily
    //  |                               |     | parsed inside a function
    //  |                               |     | which is eagerly parsed
    //  |                               |     | (Create)
    //  v                               |     v
    // +----------------------+         |    +---------+
    // | enclosing LazyScript |<--------+    | nullptr |
    // +----------------------+              +---------+
    //  |                                     |
    //  v                                     |
    //  +<------------------------------------+
    //  |
    //  | when the enclosing script     o
    //  | is successfully compiled      | when decoded from XDR,
    //  | (setEnclosingScope)           | and enclosing script is not lazy
    //  v                               | (CreateForXDR with enclosingScope)
    // +-----------------+              |
    // | enclosing Scope |<-------------+
    // +-----------------+
    GCPtr<TenuredCell*> enclosingLazyScriptOrScope_;

    // ScriptSourceObject. We leave this set to nullptr until we generate
    // bytecode for our immediate parent. This is never a CCW; we don't clone
    // LazyScripts into other compartments.
    GCPtrObject sourceObject_;

    // Heap allocated table with any free variables or inner functions.
    void* table_;

  private:
    static const uint32_t NumClosedOverBindingsBits = 20;
    static const uint32_t NumInnerFunctionsBits = 20;

    struct PackedView {
        uint32_t shouldDeclareArguments : 1;
        uint32_t hasThisBinding : 1;
        uint32_t isAsync : 1;
        uint32_t isBinAST : 1;

        uint32_t numClosedOverBindings : NumClosedOverBindingsBits;

        // -- 32bit boundary --

        uint32_t numInnerFunctions : NumInnerFunctionsBits;

        // N.B. These are booleans but need to be uint32_t to pack correctly on MSVC.
        // If you add another boolean here, make sure to initialize it in
        // LazyScript::Create().
        uint32_t isGenerator : 1;
        uint32_t strict : 1;
        uint32_t bindingsAccessedDynamically : 1;
        uint32_t hasDebuggerStatement : 1;
        uint32_t hasDirectEval : 1;
        uint32_t isLikelyConstructorWrapper : 1;
        uint32_t hasBeenCloned : 1;
        uint32_t treatAsRunOnce : 1;
        uint32_t isDerivedClassConstructor : 1;
        uint32_t needsHomeObject : 1;
        uint32_t hasRest : 1;
        uint32_t parseGoal : 1;
    };

    union {
        PackedView p_;
        uint64_t packedFields_;
    };

    // Source location for the script.
    // See the comment in JSScript for the details
    uint32_t sourceStart_;
    uint32_t sourceEnd_;
    uint32_t toStringStart_;
    uint32_t toStringEnd_;
    // Line and column of |begin_| position, that is the position where we
    // start parsing.
    uint32_t lineno_;
    uint32_t column_;

    LazyScript(JSFunction* fun, ScriptSourceObject& sourceObject,
               void* table, uint64_t packedFields,
               uint32_t begin, uint32_t end, uint32_t toStringStart,
               uint32_t lineno, uint32_t column);

    // Create a LazyScript without initializing the closedOverBindings and the
    // innerFunctions. To be GC-safe, the caller must initialize both vectors
    // with valid atoms and functions.
    static LazyScript* CreateRaw(JSContext* cx, HandleFunction fun,
                                 HandleScriptSourceObject sourceObject,
                                 uint64_t packedData, uint32_t begin, uint32_t end,
                                 uint32_t toStringStart, uint32_t lineno, uint32_t column);

  public:
    static const uint32_t NumClosedOverBindingsLimit = 1 << NumClosedOverBindingsBits;
    static const uint32_t NumInnerFunctionsLimit = 1 << NumInnerFunctionsBits;

    // Create a LazyScript and initialize closedOverBindings and innerFunctions
    // with the provided vectors.
    static LazyScript* Create(JSContext* cx, HandleFunction fun,
                              HandleScriptSourceObject sourceObject,
                              const frontend::AtomVector& closedOverBindings,
                              Handle<GCVector<JSFunction*, 8>> innerFunctions,
                              uint32_t begin, uint32_t end,
                              uint32_t toStringStart, uint32_t lineno, uint32_t column,
                              frontend::ParseGoal parseGoal);

    // Create a LazyScript and initialize the closedOverBindings and the
    // innerFunctions with dummy values to be replaced in a later initialization
    // phase.
    //
    // The "script" argument to this function can be null.  If it's non-null,
    // then this LazyScript should be associated with the given JSScript.
    //
    // The sourceObject and enclosingScope arguments may be null if the
    // enclosing function is also lazy.
    static LazyScript* CreateForXDR(JSContext* cx, HandleFunction fun,
                                    HandleScript script, HandleScope enclosingScope,
                                    HandleScriptSourceObject sourceObject,
                                    uint64_t packedData, uint32_t begin, uint32_t end,
                                    uint32_t toStringStart, uint32_t lineno, uint32_t column);

    void initRuntimeFields(uint64_t packedFields);

    static inline JSFunction* functionDelazifying(JSContext* cx, Handle<LazyScript*>);
    JSFunction* functionNonDelazifying() const {
        return function_;
    }

    JS::Compartment* compartment() const;
    JS::Compartment* maybeCompartment() const { return compartment(); }
    Realm* realm() const;

    void initScript(JSScript* script);

    JSScript* maybeScript() {
        return script_;
    }
    const JSScript* maybeScriptUnbarriered() const {
        return script_.unbarrieredGet();
    }
    bool hasScript() const {
        return bool(script_);
    }

    bool hasEnclosingScope() const {
        return enclosingLazyScriptOrScope_ &&
               enclosingLazyScriptOrScope_->is<Scope>();
    }
    bool hasEnclosingLazyScript() const {
        return enclosingLazyScriptOrScope_ &&
               enclosingLazyScriptOrScope_->is<LazyScript>();
    }

    LazyScript* enclosingLazyScript() const {
        MOZ_ASSERT(hasEnclosingLazyScript());
        return enclosingLazyScriptOrScope_->as<LazyScript>();
    }
    void setEnclosingLazyScript(LazyScript* enclosingLazyScript);

    Scope* enclosingScope() const {
        MOZ_ASSERT(hasEnclosingScope());
        return enclosingLazyScriptOrScope_->as<Scope>();
    }
    void setEnclosingScope(Scope* enclosingScope);

    bool hasNonSyntacticScope() const {
        return enclosingScope()->hasOnChain(ScopeKind::NonSyntactic);
    }

    ScriptSourceObject& sourceObject() const;
    ScriptSource* scriptSource() const {
        return sourceObject().source();
    }
    ScriptSource* maybeForwardedScriptSource() const;
    bool mutedErrors() const {
        return scriptSource()->mutedErrors();
    }

    uint32_t numClosedOverBindings() const {
        return p_.numClosedOverBindings;
    }
    JSAtom** closedOverBindings() {
        return (JSAtom**)table_;
    }

    uint32_t numInnerFunctions() const {
        return p_.numInnerFunctions;
    }
    GCPtrFunction* innerFunctions() {
        return (GCPtrFunction*)&closedOverBindings()[numClosedOverBindings()];
    }

    GeneratorKind generatorKind() const {
        return p_.isGenerator ? GeneratorKind::Generator : GeneratorKind::NotGenerator;
    }

    bool isGenerator() const { return generatorKind() == GeneratorKind::Generator; }

    void setGeneratorKind(GeneratorKind kind) {
        // A script only gets its generator kind set as part of initialization,
        // so it can only transition from NotGenerator.
        MOZ_ASSERT(!isGenerator());
        p_.isGenerator = kind == GeneratorKind::Generator;
    }

    FunctionAsyncKind asyncKind() const {
        return p_.isAsync ? FunctionAsyncKind::AsyncFunction : FunctionAsyncKind::SyncFunction;
    }
    bool isAsync() const {
        return p_.isAsync;
    }

    void setAsyncKind(FunctionAsyncKind kind) {
        p_.isAsync = kind == FunctionAsyncKind::AsyncFunction;
    }

    bool hasRest() const {
        return p_.hasRest;
    }
    void setHasRest() {
        p_.hasRest = true;
    }

    frontend::ParseGoal parseGoal() const {
        return frontend::ParseGoal(p_.parseGoal);
    }

    bool isBinAST() const {
        return p_.isBinAST;
    }
    void setIsBinAST() {
        p_.isBinAST = true;
    }

    bool strict() const {
        return p_.strict;
    }
    void setStrict() {
        p_.strict = true;
    }

    bool bindingsAccessedDynamically() const {
        return p_.bindingsAccessedDynamically;
    }
    void setBindingsAccessedDynamically() {
        p_.bindingsAccessedDynamically = true;
    }

    bool hasDebuggerStatement() const {
        return p_.hasDebuggerStatement;
    }
    void setHasDebuggerStatement() {
        p_.hasDebuggerStatement = true;
    }

    bool hasDirectEval() const {
        return p_.hasDirectEval;
    }
    void setHasDirectEval() {
        p_.hasDirectEval = true;
    }

    bool isLikelyConstructorWrapper() const {
        return p_.isLikelyConstructorWrapper;
    }
    void setLikelyConstructorWrapper() {
        p_.isLikelyConstructorWrapper = true;
    }

    bool hasBeenCloned() const {
        return p_.hasBeenCloned;
    }
    void setHasBeenCloned() {
        p_.hasBeenCloned = true;
    }

    bool treatAsRunOnce() const {
        return p_.treatAsRunOnce;
    }
    void setTreatAsRunOnce() {
        p_.treatAsRunOnce = true;
    }

    bool isDerivedClassConstructor() const {
        return p_.isDerivedClassConstructor;
    }
    void setIsDerivedClassConstructor() {
        p_.isDerivedClassConstructor = true;
    }

    bool needsHomeObject() const {
        return p_.needsHomeObject;
    }
    void setNeedsHomeObject() {
        p_.needsHomeObject = true;
    }

    bool shouldDeclareArguments() const {
        return p_.shouldDeclareArguments;
    }
    void setShouldDeclareArguments() {
        p_.shouldDeclareArguments = true;
    }

    bool hasThisBinding() const {
        return p_.hasThisBinding;
    }
    void setHasThisBinding() {
        p_.hasThisBinding = true;
    }

    const char* filename() const {
        return scriptSource()->filename();
    }
    uint32_t sourceStart() const {
        return sourceStart_;
    }
    uint32_t sourceEnd() const {
        return sourceEnd_;
    }
    uint32_t sourceLength() const {
        return sourceEnd_ - sourceStart_;
    }
    uint32_t toStringStart() const {
        return toStringStart_;
    }
    uint32_t toStringEnd() const {
        return toStringEnd_;
    }
    uint32_t lineno() const {
        return lineno_;
    }
    uint32_t column() const {
        return column_;
    }

    void setToStringEnd(uint32_t toStringEnd) {
        MOZ_ASSERT(toStringStart_ <= toStringEnd);
        MOZ_ASSERT(toStringEnd_ >= sourceEnd_);
        toStringEnd_ = toStringEnd;
    }

    // Returns true if the enclosing script has ever been compiled.
    // Once the enclosing script is compiled, the scope chain is created.
    // This LazyScript is delazify-able as long as it has the enclosing scope,
    // even if the enclosing JSScript is GCed.
    // The enclosing JSScript can be GCed later if the enclosing scope is not
    // FunctionScope or ModuleScope.
    bool enclosingScriptHasEverBeenCompiled() const {
        return hasEnclosingScope();
    }

    friend class GCMarker;
    void traceChildren(JSTracer* trc);
    void finalize(js::FreeOp* fop);

    static const JS::TraceKind TraceKind = JS::TraceKind::LazyScript;

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf)
    {
        return mallocSizeOf(table_);
    }

    uint64_t packedFields() const {
        return packedFields_;
    }
};

/* If this fails, add/remove padding within LazyScript. */
static_assert(sizeof(LazyScript) % js::gc::CellAlignBytes == 0,
              "Size of LazyScript must be an integral multiple of js::gc::CellAlignBytes");

struct ScriptAndCounts
{
    /* This structure is stored and marked from the JSRuntime. */
    JSScript* script;
    ScriptCounts scriptCounts;

    inline explicit ScriptAndCounts(JSScript* script);
    inline ScriptAndCounts(ScriptAndCounts&& sac);

    const PCCounts* maybeGetPCCounts(jsbytecode* pc) const {
        return scriptCounts.maybeGetPCCounts(script->pcToOffset(pc));
    }
    const PCCounts* maybeGetThrowCounts(jsbytecode* pc) const {
        return scriptCounts.maybeGetThrowCounts(script->pcToOffset(pc));
    }

    jit::IonScriptCounts* getIonCounts() const {
        return scriptCounts.ionCounts_;
    }

    void trace(JSTracer* trc) {
        TraceRoot(trc, &script, "ScriptAndCounts::script");
    }
};

extern char*
FormatIntroducedFilename(JSContext* cx, const char* filename, unsigned lineno,
                         const char* introducer);

struct GSNCache;

jssrcnote*
GetSrcNote(GSNCache& cache, JSScript* script, jsbytecode* pc);

extern jssrcnote*
GetSrcNote(JSContext* cx, JSScript* script, jsbytecode* pc);

extern jsbytecode*
LineNumberToPC(JSScript* script, unsigned lineno);

extern JS_FRIEND_API(unsigned)
GetScriptLineExtent(JSScript* script);

} /* namespace js */

namespace js {

extern unsigned
PCToLineNumber(JSScript* script, jsbytecode* pc, unsigned* columnp = nullptr);

extern unsigned
PCToLineNumber(unsigned startLine, jssrcnote* notes, jsbytecode* code, jsbytecode* pc,
               unsigned* columnp = nullptr);

/*
 * This function returns the file and line number of the script currently
 * executing on cx. If there is no current script executing on cx (e.g., a
 * native called directly through JSAPI (e.g., by setTimeout)), nullptr and 0
 * are returned as the file and line.
 */
extern void
DescribeScriptedCallerForCompilation(JSContext* cx, MutableHandleScript maybeScript,
                                     const char** file, unsigned* linenop, uint32_t* pcOffset,
                                     bool* mutedErrors);

/*
 * Like DescribeScriptedCallerForCompilation, but this function avoids looking
 * up the script/pc and the full linear scan to compute line number.
 */
extern void
DescribeScriptedCallerForDirectEval(JSContext* cx, HandleScript script, jsbytecode* pc,
                                    const char** file, unsigned* linenop, uint32_t* pcOffset,
                                    bool* mutedErrors);

JSScript*
CloneScriptIntoFunction(JSContext* cx, HandleScope enclosingScope, HandleFunction fun,
                        HandleScript src);

JSScript*
CloneGlobalScript(JSContext* cx, ScopeKind scopeKind, HandleScript src);

} /* namespace js */

// JS::ubi::Nodes can point to js::LazyScripts; they're js::gc::Cell instances
// with no associated compartment.
namespace JS {
namespace ubi {
template<>
class Concrete<js::LazyScript> : TracerConcrete<js::LazyScript> {
  protected:
    explicit Concrete(js::LazyScript *ptr) : TracerConcrete<js::LazyScript>(ptr) { }

  public:
    static void construct(void *storage, js::LazyScript *ptr) { new (storage) Concrete(ptr); }

    CoarseType coarseType() const final { return CoarseType::Script; }
    Size size(mozilla::MallocSizeOf mallocSizeOf) const override;
    const char* scriptFilename() const final;

    const char16_t* typeName() const override { return concreteTypeName; }
    static const char16_t concreteTypeName[];
};
} // namespace ubi
} // namespace JS

#endif /* vm_JSScript_h */
