/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <iostream>
#include <vector>

#include "android-base/stringprintf.h"

#include "base/logging.h"
#include "base/macros.h"
#include "jit/jit.h"
#include "jni.h"
#include "native_stack_dump.h"
#include "jvmti.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"
#include "thread_list.h"

#include "ti-agent/common_helper.h"
#include "ti-agent/common_load.h"

namespace art {
namespace Test913Heaps {

using android::base::StringPrintf;

extern "C" JNIEXPORT void JNICALL Java_Main_forceGarbageCollection(JNIEnv* env ATTRIBUTE_UNUSED,
                                                                   jclass klass ATTRIBUTE_UNUSED) {
  jvmtiError ret = jvmti_env->ForceGarbageCollection();
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Error forcing a garbage collection: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
  }
}

class IterationConfig {
 public:
  IterationConfig() {}
  virtual ~IterationConfig() {}

  virtual jint Handle(jvmtiHeapReferenceKind reference_kind,
                      const jvmtiHeapReferenceInfo* reference_info,
                      jlong class_tag,
                      jlong referrer_class_tag,
                      jlong size,
                      jlong* tag_ptr,
                      jlong* referrer_tag_ptr,
                      jint length,
                      void* user_data) = 0;
};

static jint JNICALL HeapReferenceCallback(jvmtiHeapReferenceKind reference_kind,
                                          const jvmtiHeapReferenceInfo* reference_info,
                                          jlong class_tag,
                                          jlong referrer_class_tag,
                                          jlong size,
                                          jlong* tag_ptr,
                                          jlong* referrer_tag_ptr,
                                          jint length,
                                          void* user_data) {
  IterationConfig* config = reinterpret_cast<IterationConfig*>(user_data);
  return config->Handle(reference_kind,
                        reference_info,
                        class_tag,
                        referrer_class_tag,
                        size,
                        tag_ptr,
                        referrer_tag_ptr,
                        length,
                        user_data);
}

static bool Run(jint heap_filter,
                jclass klass_filter,
                jobject initial_object,
                IterationConfig* config) {
  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_reference_callback = HeapReferenceCallback;

  jvmtiError ret = jvmti_env->FollowReferences(heap_filter,
                                               klass_filter,
                                               initial_object,
                                               &callbacks,
                                               config);
  if (ret != JVMTI_ERROR_NONE) {
    char* err;
    jvmti_env->GetErrorName(ret, &err);
    printf("Failure running FollowReferences: %s\n", err);
    jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(err));
    return false;
  }
  return true;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_followReferences(JNIEnv* env,
                                                                     jclass klass ATTRIBUTE_UNUSED,
                                                                     jint heap_filter,
                                                                     jclass klass_filter,
                                                                     jobject initial_object,
                                                                     jint stop_after,
                                                                     jint follow_set,
                                                                     jobject jniRef) {
  class PrintIterationConfig FINAL : public IterationConfig {
   public:
    PrintIterationConfig(jint _stop_after, jint _follow_set)
        : counter_(0),
          stop_after_(_stop_after),
          follow_set_(_follow_set) {
    }

    jint Handle(jvmtiHeapReferenceKind reference_kind,
                const jvmtiHeapReferenceInfo* reference_info,
                jlong class_tag,
                jlong referrer_class_tag,
                jlong size,
                jlong* tag_ptr,
                jlong* referrer_tag_ptr,
                jint length,
                void* user_data ATTRIBUTE_UNUSED) OVERRIDE {
      jlong tag = *tag_ptr;
      // Only check tagged objects.
      if (tag == 0) {
        return JVMTI_VISIT_OBJECTS;
      }

      Print(reference_kind,
            reference_info,
            class_tag,
            referrer_class_tag,
            size,
            tag_ptr,
            referrer_tag_ptr,
            length);

      counter_++;
      if (counter_ == stop_after_) {
        return JVMTI_VISIT_ABORT;
      }

      if (tag > 0 && tag < 32) {
        bool should_visit_references = (follow_set_ & (1 << static_cast<int32_t>(tag))) != 0;
        return should_visit_references ? JVMTI_VISIT_OBJECTS : 0;
      }

      return JVMTI_VISIT_OBJECTS;
    }

    void Print(jvmtiHeapReferenceKind reference_kind,
               const jvmtiHeapReferenceInfo* reference_info,
               jlong class_tag,
               jlong referrer_class_tag,
               jlong size,
               jlong* tag_ptr,
               jlong* referrer_tag_ptr,
               jint length) {
      std::string referrer_str;
      if (referrer_tag_ptr == nullptr) {
        referrer_str = "root@root";
      } else {
        referrer_str = StringPrintf("%" PRId64 "@%" PRId64, *referrer_tag_ptr, referrer_class_tag);
      }

      jlong adapted_size = size;
      if (*tag_ptr >= 1000) {
        // This is a class or interface, the size of which will be dependent on the architecture.
        // Do not print the size, but detect known values and "normalize" for the golden file.
        if ((sizeof(void*) == 4 && size == 172) || (sizeof(void*) == 8 && size == 224)) {
          adapted_size = 123;
        }
      }

      std::string referree_str = StringPrintf("%" PRId64 "@%" PRId64, *tag_ptr, class_tag);

      lines_.push_back(CreateElem(referrer_str,
                                  referree_str,
                                  reference_kind,
                                  reference_info,
                                  adapted_size,
                                  length));

      if (reference_kind == JVMTI_HEAP_REFERENCE_THREAD && *tag_ptr == 1000) {
        DumpStacks();
      }
    }

    std::vector<std::string> GetLines() const {
      std::vector<std::string> ret;
      for (const std::unique_ptr<Elem>& e : lines_) {
        ret.push_back(e->Print());
      }
      return ret;
    }

   private:
    // We need to postpone some printing, as required functions are not callback-safe.
    class Elem {
     public:
      Elem(const std::string& referrer, const std::string& referree, jlong size, jint length)
          : referrer_(referrer), referree_(referree), size_(size), length_(length) {}
      virtual ~Elem() {}

      std::string Print() const {
        return StringPrintf("%s --(%s)--> %s [size=%" PRId64 ", length=%d]",
                            referrer_.c_str(),
                            PrintArrowType().c_str(),
                            referree_.c_str(),
                            size_,
                            length_);
      }

     protected:
      virtual std::string PrintArrowType() const = 0;

     private:
      std::string referrer_;
      std::string referree_;
      jlong size_;
      jint length_;
    };

    class JNILocalElement : public Elem {
     public:
      JNILocalElement(const std::string& referrer,
                      const std::string& referree,
                      jlong size,
                      jint length,
                      const jvmtiHeapReferenceInfo* reference_info)
          : Elem(referrer, referree, size, length) {
        memcpy(&info_, reference_info, sizeof(jvmtiHeapReferenceInfo));
      }

     protected:
      std::string PrintArrowType() const OVERRIDE {
        char* name = nullptr;
        if (info_.jni_local.method != nullptr) {
          jvmti_env->GetMethodName(info_.jni_local.method, &name, nullptr, nullptr);
        }
        std::string ret = StringPrintf("jni-local[id=%" PRId64 ",tag=%" PRId64 ",depth=%d,"
                                       "method=%s]",
                                       info_.jni_local.thread_id,
                                       info_.jni_local.thread_tag,
                                       info_.jni_local.depth,
                                       name == nullptr ? "<null>" : name);
        if (name != nullptr) {
          jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(name));
        }

        return ret;
      }

     private:
      const std::string string_;
      jvmtiHeapReferenceInfo info_;
    };

    class StackLocalElement : public Elem {
     public:
      StackLocalElement(const std::string& referrer,
                        const std::string& referree,
                        jlong size,
                        jint length,
                        const jvmtiHeapReferenceInfo* reference_info)
          REQUIRES_SHARED(Locks::mutator_lock_)
          : Elem(referrer, referree, size, length) {
        memcpy(&info_, reference_info, sizeof(jvmtiHeapReferenceInfo));
        // Debug stack trace for failure condition. Remove when done.
        if (info_.stack_local.depth == 3 && info_.stack_local.slot == 13) {
          DumpNativeStack(std::cerr, GetTid());
          Thread::Current()->DumpJavaStack(std::cerr, false, false);
        }
      }

     protected:
      std::string PrintArrowType() const OVERRIDE {
        char* name = nullptr;
        if (info_.stack_local.method != nullptr) {
          jvmti_env->GetMethodName(info_.stack_local.method, &name, nullptr, nullptr);
        }
        std::string ret = StringPrintf("stack-local[id=%" PRId64 ",tag=%" PRId64 ",depth=%d,"
                                       "method=%s,vreg=%d,location=% " PRId64 "]",
                                       info_.stack_local.thread_id,
                                       info_.stack_local.thread_tag,
                                       info_.stack_local.depth,
                                       name == nullptr ? "<null>" : name,
                                       info_.stack_local.slot,
                                       info_.stack_local.location);
        if (name != nullptr) {
          jvmti_env->Deallocate(reinterpret_cast<unsigned char*>(name));
        }

        return ret;
      }

     private:
      const std::string string_;
      jvmtiHeapReferenceInfo info_;
    };

    // For simple or unimplemented cases.
    class StringElement : public Elem {
     public:
      StringElement(const std::string& referrer,
                   const std::string& referree,
                   jlong size,
                   jint length,
                   const std::string& string)
          : Elem(referrer, referree, size, length), string_(string) {}

     protected:
      std::string PrintArrowType() const OVERRIDE {
        return string_;
      }

     private:
      const std::string string_;
    };

    static std::unique_ptr<Elem> CreateElem(const std::string& referrer,
                                            const std::string& referree,
                                            jvmtiHeapReferenceKind reference_kind,
                                            const jvmtiHeapReferenceInfo* reference_info,
                                            jlong size,
                                            jint length) {
      switch (reference_kind) {
        case JVMTI_HEAP_REFERENCE_CLASS:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "class"));
        case JVMTI_HEAP_REFERENCE_FIELD: {
          std::string tmp = StringPrintf("field@%d", reference_info->field.index);
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                        referree,
                                                        size,
                                                        length,
                                                        tmp));
        }
        case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT: {
          std::string tmp = StringPrintf("array-element@%d", reference_info->array.index);
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         tmp));
        }
        case JVMTI_HEAP_REFERENCE_CLASS_LOADER:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "classloader"));
        case JVMTI_HEAP_REFERENCE_SIGNERS:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "signers"));
        case JVMTI_HEAP_REFERENCE_PROTECTION_DOMAIN:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "protection-domain"));
        case JVMTI_HEAP_REFERENCE_INTERFACE:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "interface"));
        case JVMTI_HEAP_REFERENCE_STATIC_FIELD: {
          std::string tmp = StringPrintf("array-element@%d", reference_info->array.index);
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         tmp));;
        }
        case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "constant-pool"));
        case JVMTI_HEAP_REFERENCE_SUPERCLASS:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "superclass"));
        case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "jni-global"));
        case JVMTI_HEAP_REFERENCE_SYSTEM_CLASS:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "system-class"));
        case JVMTI_HEAP_REFERENCE_MONITOR:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "monitor"));
        case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
          return std::unique_ptr<Elem>(new StackLocalElement(referrer,
                                                             referree,
                                                             size,
                                                             length,
                                                             reference_info));
        case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
          return std::unique_ptr<Elem>(new JNILocalElement(referrer,
                                                           referree,
                                                           size,
                                                           length,
                                                           reference_info));
        case JVMTI_HEAP_REFERENCE_THREAD:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "thread"));
        case JVMTI_HEAP_REFERENCE_OTHER:
          return std::unique_ptr<Elem>(new StringElement(referrer,
                                                         referree,
                                                         size,
                                                         length,
                                                         "other"));
      }
      LOG(FATAL) << "Unknown kind";
      UNREACHABLE();
    }

    static void DumpStacks() NO_THREAD_SAFETY_ANALYSIS {
      auto dump_function = [](art::Thread* t, void* data ATTRIBUTE_UNUSED) {
        std::string name;
        t->GetThreadName(name);
        LOG(ERROR) << name;
        art::DumpNativeStack(LOG_STREAM(ERROR), t->GetTid());
      };
      art::Runtime::Current()->GetThreadList()->ForEach(dump_function, nullptr);
    }

    jint counter_;
    const jint stop_after_;
    const jint follow_set_;

    std::vector<std::unique_ptr<Elem>> lines_;
  };

  jit::ScopedJitSuspend sjs;  // Wait to avoid JIT influence (e.g., JNI globals).

  // If jniRef isn't null, add a local and a global ref.
  ScopedLocalRef<jobject> jni_local_ref(env, nullptr);
  jobject jni_global_ref = nullptr;
  if (jniRef != nullptr) {
    jni_local_ref.reset(env->NewLocalRef(jniRef));
    jni_global_ref = env->NewGlobalRef(jniRef);
  }

  PrintIterationConfig config(stop_after, follow_set);
  Run(heap_filter, klass_filter, initial_object, &config);

  std::vector<std::string> lines = config.GetLines();
  jobjectArray ret = CreateObjectArray(env,
                                       static_cast<jint>(lines.size()),
                                       "java/lang/String",
                                       [&](jint i) {
                                         return env->NewStringUTF(lines[i].c_str());
                                       });

  if (jni_global_ref != nullptr) {
    env->DeleteGlobalRef(jni_global_ref);
  }

  return ret;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_Main_followReferencesString(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject initial_object) {
  struct FindStringCallbacks {
    static jint JNICALL FollowReferencesCallback(
        jvmtiHeapReferenceKind reference_kind ATTRIBUTE_UNUSED,
        const jvmtiHeapReferenceInfo* reference_info ATTRIBUTE_UNUSED,
        jlong class_tag ATTRIBUTE_UNUSED,
        jlong referrer_class_tag ATTRIBUTE_UNUSED,
        jlong size ATTRIBUTE_UNUSED,
        jlong* tag_ptr ATTRIBUTE_UNUSED,
        jlong* referrer_tag_ptr ATTRIBUTE_UNUSED,
        jint length ATTRIBUTE_UNUSED,
        void* user_data ATTRIBUTE_UNUSED) {
      return JVMTI_VISIT_OBJECTS;  // Continue visiting.
    }

    static jint JNICALL StringValueCallback(jlong class_tag,
                                            jlong size,
                                            jlong* tag_ptr,
                                            const jchar* value,
                                            jint value_length,
                                            void* user_data) {
      FindStringCallbacks* p = reinterpret_cast<FindStringCallbacks*>(user_data);
      if (*tag_ptr != 0) {
        size_t utf_byte_count = CountUtf8Bytes(value, value_length);
        std::unique_ptr<char[]> mod_utf(new char[utf_byte_count + 1]);
        memset(mod_utf.get(), 0, utf_byte_count + 1);
        ConvertUtf16ToModifiedUtf8(mod_utf.get(), utf_byte_count, value, value_length);
        p->data.push_back(android::base::StringPrintf("%" PRId64 "@%" PRId64 " (%" PRId64 ", '%s')",
                                                      *tag_ptr,
                                                      class_tag,
                                                      size,
                                                      mod_utf.get()));
        // Update the tag to test whether that works.
        *tag_ptr = *tag_ptr + 1;
      }
      return 0;
    }

    std::vector<std::string> data;
  };

  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_reference_callback = FindStringCallbacks::FollowReferencesCallback;
  callbacks.string_primitive_value_callback = FindStringCallbacks::StringValueCallback;

  FindStringCallbacks fsc;
  jvmtiError ret = jvmti_env->FollowReferences(0, nullptr, initial_object, &callbacks, &fsc);
  if (JvmtiErrorToException(env, ret)) {
    return nullptr;
  }

  jobjectArray retArray = CreateObjectArray(env,
                                            static_cast<jint>(fsc.data.size()),
                                            "java/lang/String",
                                            [&](jint i) {
                                              return env->NewStringUTF(fsc.data[i].c_str());
                                            });
  return retArray;
}


extern "C" JNIEXPORT jstring JNICALL Java_Main_followReferencesPrimitiveArray(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject initial_object) {
  struct FindArrayCallbacks {
    static jint JNICALL FollowReferencesCallback(
        jvmtiHeapReferenceKind reference_kind ATTRIBUTE_UNUSED,
        const jvmtiHeapReferenceInfo* reference_info ATTRIBUTE_UNUSED,
        jlong class_tag ATTRIBUTE_UNUSED,
        jlong referrer_class_tag ATTRIBUTE_UNUSED,
        jlong size ATTRIBUTE_UNUSED,
        jlong* tag_ptr ATTRIBUTE_UNUSED,
        jlong* referrer_tag_ptr ATTRIBUTE_UNUSED,
        jint length ATTRIBUTE_UNUSED,
        void* user_data ATTRIBUTE_UNUSED) {
      return JVMTI_VISIT_OBJECTS;  // Continue visiting.
    }

    static jint JNICALL ArrayValueCallback(jlong class_tag,
                                           jlong size,
                                           jlong* tag_ptr,
                                           jint element_count,
                                           jvmtiPrimitiveType element_type,
                                           const void* elements,
                                           void* user_data) {
      FindArrayCallbacks* p = reinterpret_cast<FindArrayCallbacks*>(user_data);
      if (*tag_ptr != 0) {
        std::ostringstream oss;
        oss << *tag_ptr
            << '@'
            << class_tag
            << " ("
            << size
            << ", "
            << element_count
            << "x"
            << static_cast<char>(element_type)
            << " '";
        size_t element_size;
        switch (element_type) {
          case JVMTI_PRIMITIVE_TYPE_BOOLEAN:
          case JVMTI_PRIMITIVE_TYPE_BYTE:
            element_size = 1;
            break;
          case JVMTI_PRIMITIVE_TYPE_CHAR:
          case JVMTI_PRIMITIVE_TYPE_SHORT:
            element_size = 2;
            break;
          case JVMTI_PRIMITIVE_TYPE_INT:
          case JVMTI_PRIMITIVE_TYPE_FLOAT:
            element_size = 4;
            break;
          case JVMTI_PRIMITIVE_TYPE_LONG:
          case JVMTI_PRIMITIVE_TYPE_DOUBLE:
            element_size = 8;
            break;
          default:
            LOG(FATAL) << "Unknown type " << static_cast<size_t>(element_type);
            UNREACHABLE();
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(elements);
        for (size_t i = 0; i != element_size * element_count; ++i) {
          oss << android::base::StringPrintf("%02x", data[i]);
        }
        oss << "')";

        if (!p->data.empty()) {
          p->data += "\n";
        }
        p->data += oss.str();
        // Update the tag to test whether that works.
        *tag_ptr = *tag_ptr + 1;
      }
      return 0;
    }

    std::string data;
  };

  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_reference_callback = FindArrayCallbacks::FollowReferencesCallback;
  callbacks.array_primitive_value_callback = FindArrayCallbacks::ArrayValueCallback;

  FindArrayCallbacks fac;
  jvmtiError ret = jvmti_env->FollowReferences(0, nullptr, initial_object, &callbacks, &fac);
  if (JvmtiErrorToException(env, ret)) {
    return nullptr;
  }
  return env->NewStringUTF(fac.data.c_str());
}

static constexpr const char* GetPrimitiveTypeName(jvmtiPrimitiveType type) {
  switch (type) {
    case JVMTI_PRIMITIVE_TYPE_BOOLEAN:
      return "boolean";
    case JVMTI_PRIMITIVE_TYPE_BYTE:
      return "byte";
    case JVMTI_PRIMITIVE_TYPE_CHAR:
      return "char";
    case JVMTI_PRIMITIVE_TYPE_SHORT:
      return "short";
    case JVMTI_PRIMITIVE_TYPE_INT:
      return "int";
    case JVMTI_PRIMITIVE_TYPE_FLOAT:
      return "float";
    case JVMTI_PRIMITIVE_TYPE_LONG:
      return "long";
    case JVMTI_PRIMITIVE_TYPE_DOUBLE:
      return "double";
  }
  LOG(FATAL) << "Unknown type " << static_cast<size_t>(type);
  UNREACHABLE();
}

extern "C" JNIEXPORT jstring JNICALL Java_Main_followReferencesPrimitiveFields(
    JNIEnv* env, jclass klass ATTRIBUTE_UNUSED, jobject initial_object) {
  struct FindFieldCallbacks {
    static jint JNICALL FollowReferencesCallback(
        jvmtiHeapReferenceKind reference_kind ATTRIBUTE_UNUSED,
        const jvmtiHeapReferenceInfo* reference_info ATTRIBUTE_UNUSED,
        jlong class_tag ATTRIBUTE_UNUSED,
        jlong referrer_class_tag ATTRIBUTE_UNUSED,
        jlong size ATTRIBUTE_UNUSED,
        jlong* tag_ptr ATTRIBUTE_UNUSED,
        jlong* referrer_tag_ptr ATTRIBUTE_UNUSED,
        jint length ATTRIBUTE_UNUSED,
        void* user_data ATTRIBUTE_UNUSED) {
      return JVMTI_VISIT_OBJECTS;  // Continue visiting.
    }

    static jint JNICALL PrimitiveFieldValueCallback(jvmtiHeapReferenceKind kind,
                                                    const jvmtiHeapReferenceInfo* info,
                                                    jlong class_tag,
                                                    jlong* tag_ptr,
                                                    jvalue value,
                                                    jvmtiPrimitiveType value_type,
                                                    void* user_data) {
      FindFieldCallbacks* p = reinterpret_cast<FindFieldCallbacks*>(user_data);
      if (*tag_ptr != 0) {
        std::ostringstream oss;
        oss << *tag_ptr
            << '@'
            << class_tag
            << " ("
            << (kind == JVMTI_HEAP_REFERENCE_FIELD ? "instance, " : "static, ")
            << GetPrimitiveTypeName(value_type)
            << ", index="
            << info->field.index
            << ") ";
        // Be lazy, always print eight bytes.
        static_assert(sizeof(jvalue) == sizeof(uint64_t), "Unexpected jvalue size");
        uint64_t val;
        memcpy(&val, &value, sizeof(uint64_t));  // To avoid undefined behavior.
        oss << android::base::StringPrintf("%016" PRIx64, val);

        if (!p->data.empty()) {
          p->data += "\n";
        }
        p->data += oss.str();
        // Update the tag to test whether that works.
        *tag_ptr = *tag_ptr + 1;
      }
      return 0;
    }

    std::string data;
  };

  jvmtiHeapCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiHeapCallbacks));
  callbacks.heap_reference_callback = FindFieldCallbacks::FollowReferencesCallback;
  callbacks.primitive_field_callback = FindFieldCallbacks::PrimitiveFieldValueCallback;

  FindFieldCallbacks ffc;
  jvmtiError ret = jvmti_env->FollowReferences(0, nullptr, initial_object, &callbacks, &ffc);
  if (JvmtiErrorToException(env, ret)) {
    return nullptr;
  }
  return env->NewStringUTF(ffc.data.c_str());
}

}  // namespace Test913Heaps
}  // namespace art
