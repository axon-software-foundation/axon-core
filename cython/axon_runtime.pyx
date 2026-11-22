# cython/axon_runtime.pyx
# ─────────────────────────
# Cython bindings: exposes the C++ runtime to Python.
# This is the ONLY file that crosses the C++/Python boundary.
#
# Build with:  python setup.py build_ext --inplace
#
# distutils: language = c++
# distutils: extra_compile_args = -std=c++17 -O2

from libcpp.string  cimport string
from libcpp.vector  cimport vector
from libcpp.memory  cimport shared_ptr, make_shared
from libcpp         cimport bool as cbool

# ─── Declare C++ types ─────────────────────────────────────────
cdef extern from "../runtime/interfaces/interfaces.hpp" namespace "axon":

    cdef cppclass AgentID:
        string value

    cdef cppclass SessionID:
        string value

    cdef cppclass FlowID:
        string value

    cdef enum class AgentState "axon::AgentState":
        IDLE, PENDING, READY, RUNNING, CHECKPOINT,
        EMITTING, SUCCESS, FAILED, FLAGGED

    string agent_state_name(AgentState s)

    cdef cppclass AxonValue:
        cbool is_null()
        cbool is_truthy()

        @staticmethod
        AxonValue null()

        @staticmethod
        AxonValue from_bool "from"(cbool v)

        @staticmethod
        AxonValue from_double "from"(double v)

        @staticmethod
        AxonValue from_str "from"(string v)

    cdef cppclass IActor:
        AgentID id()
        AgentState state()
        cbool transition(AgentState next)
        AxonValue run(const AxonValue& input)
        void checkpoint()
        void reset()

    cdef cppclass IScheduler:
        void start(size_t thread_count)
        void stop()
        void submit(...)
        AxonValue await_result(const AgentID& id)
        void cancel(const AgentID& id)
        size_t queue_depth()

    cdef cppclass IMemoryEngine:
        void store(const string& key, const AxonValue& value)
        AxonValue retrieve(const string& key)
        cbool exists(const string& key)
        void forget(const string& key)
        void clear()
        string type_name()

    cdef cppclass ISessionEngine:
        SessionID create()
        void persist(const SessionID& id)
        cbool rehydrate(const SessionID& id)
        void close(const SessionID& id)


# ─── Python-visible wrapper classes ────────────────────────────

cdef class PyAxonValue:
    """
    Thin Python wrapper around AxonValue.
    Converts automatically from/to Python primitives.
    """
    cdef AxonValue _val

    @staticmethod
    def null():
        v = PyAxonValue()
        v._val = AxonValue.null()
        return v

    @staticmethod
    def from_python(obj):
        cdef PyAxonValue v = PyAxonValue()
        if obj is None:
            v._val = AxonValue.null()
        elif isinstance(obj, bool):
            v._val = AxonValue.from_bool(obj)
        elif isinstance(obj, (int, float)):
            v._val = AxonValue.from_double(float(obj))
        elif isinstance(obj, str):
            v._val = AxonValue.from_str(obj.encode("utf-8"))
        else:
            raise TypeError(f"Cannot convert {type(obj)} to AxonValue")
        return v

    def is_null(self):
        return self._val.is_null()

    def is_truthy(self):
        return self._val.is_truthy()


cdef class PyAgentState:
    """Exposes AgentState enum to Python."""
    IDLE       = 0
    PENDING    = 1
    READY      = 2
    RUNNING    = 3
    CHECKPOINT = 4
    EMITTING   = 5
    SUCCESS    = 6
    FAILED     = 7
    FLAGGED    = 8

    @staticmethod
    def name(int state_int):
        cdef AgentState s = <AgentState>state_int
        return agent_state_name(s).decode("utf-8")


cdef class PyMemoryEngine:
    """
    Python interface to the C++ IMemoryEngine.
    Phase 1: wraps RuntimeMemory.
    Phase 2: wraps SemanticMemory, PersistentMemory, etc.
    """
    cdef shared_ptr[IMemoryEngine] _engine

    def store(self, str key, object value):
        cdef AxonValue v = PyAxonValue.from_python(value)._val
        self._engine.get().store(key.encode("utf-8"), v)

    def retrieve(self, str key):
        cdef AxonValue v = self._engine.get().retrieve(key.encode("utf-8"))
        if v.is_null():
            return None
        return v   # TODO: convert back to Python in Phase 2

    def exists(self, str key):
        return self._engine.get().exists(key.encode("utf-8"))

    def forget(self, str key):
        self._engine.get().forget(key.encode("utf-8"))

    def clear(self):
        self._engine.get().clear()

    def type_name(self):
        return self._engine.get().type_name().decode("utf-8")


# ─── Public API ────────────────────────────────────────────────

def get_runtime_version():
    return "0.1.0-phase1"

def state_name(int state_int):
    """Convert integer state to name string."""
    return PyAgentState.name(state_int)
