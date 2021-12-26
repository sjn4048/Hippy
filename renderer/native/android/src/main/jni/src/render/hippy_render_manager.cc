#include "render/hippy_render_manager.h"

#include <cstdint>
#include <iostream>
#include <utility>

#include "base/logging.h"
#include "jni/jni_env.h"

#include "dom/taitank_layout_node.h"

constexpr char kId[] = "id";
constexpr char kPid[] = "pId";
constexpr char kIndex[] = "index";
constexpr char kName[] = "name";
constexpr char kWidth[] = "width";
constexpr char kHeight[] = "height";
constexpr char kLeft[] = "left";
constexpr char kTop[] = "top";
constexpr char kProps[] = "props";

namespace hippy {
inline namespace dom {

void HippyRenderManager::CreateRenderNode(std::vector<std::shared_ptr<hippy::dom::DomNode>>&& nodes) {
  serializer_->Release();
  serializer_->WriteHeader();

  uint32_t len = nodes.size();
  tdf::base::DomValue::DomValueArrayType dom_node_array;
  dom_node_array.resize(len);
  for (uint32_t i = 0; i < len; i++) {
    tdf::base::DomValue::DomValueObjectType dom_node;
    dom_node[kId] = tdf::base::DomValue(nodes[i]->GetId());
    dom_node[kPid] = tdf::base::DomValue(nodes[i]->GetPid());
    dom_node[kIndex] = tdf::base::DomValue(nodes[i]->GetIndex());
    dom_node[kName] = tdf::base::DomValue(nodes[i]->GetViewName());

    if (nodes[i]->GetViewName() == "Text") {
      int32_t id = nodes[i]->GetId();
      TaitankMeasureFunction measure_function = [this, id](HPNodeRef node, float width, MeasureMode widthMeasureMode,
                                                           float height, MeasureMode heightMeasureMode,
                                                           void* layoutContext) -> TaitankResult {
        int64_t result;
        this->CallNativeMeasureMethod(id, width, widthMeasureMode, height, heightMeasureMode, 0, 0, 0, 0, result);
        TaitankResult layout_result;
        layout_result.width = (int32_t)(0xFFFFFFFF & (result >> 32));
        layout_result.height = (int32_t)(0xFFFFFFFF & result);
        TDF_BASE_LOG(INFO) << "measure width: " << layout_result.width << ", height: " << layout_result.height << ", result: " << result;
        return layout_result;
      };
      std::static_pointer_cast<TaitankLayoutNode>(nodes[i]->GetLayoutNode())->SetMeasureFunction(measure_function);
    }

    tdf::base::DomValue::DomValueObjectType props;
    // 样式属性
    auto style = nodes[i]->GetStyleMap();
    auto iter = style.begin();
    while (iter != style.end()) {
      props[iter->first] = *(iter->second);
      iter++;
    }

    // 用户自定义属性
    auto dom_ext = nodes[i]->GetExtStyle();
    iter = dom_ext.begin();
    while (iter != dom_ext.end()) {
      props[iter->first] = *(iter->second);
      iter++;
    }

    dom_node[kProps] = props;
    dom_node_array[i] = dom_node;
  }
  serializer_->WriteDenseJSArray(dom_node_array);
  std::pair<uint8_t*, size_t> buffer_pair = serializer_->Release();

  CallNativeMethod(buffer_pair, "createNode");
  return;
};

void HippyRenderManager::UpdateRenderNode(std::vector<std::shared_ptr<DomNode>>&& nodes) {
  serializer_->Release();
  serializer_->WriteHeader();

  uint32_t len = nodes.size();
  tdf::base::DomValue::DomValueArrayType dom_node_array;
  dom_node_array.resize(len);
  for (uint32_t i = 0; i < len; i++) {
    tdf::base::DomValue::DomValueObjectType dom_node;
    dom_node[kId] = tdf::base::DomValue(nodes[i]->GetId());
    dom_node[kPid] = tdf::base::DomValue(nodes[i]->GetPid());
    dom_node[kIndex] = tdf::base::DomValue(nodes[i]->GetIndex());
    dom_node[kName] = tdf::base::DomValue(nodes[i]->GetViewName());

    tdf::base::DomValue::DomValueObjectType props;
    // diff 属性
    auto diff = nodes[i]->GetDiffStyle();
    auto iter = diff.begin();
    while (iter != diff.end()) {
      props[iter->first] = *(iter->second);
      iter++;
    }
    dom_node[kProps] = props;
    dom_node_array[i] = dom_node;
  }
  serializer_->WriteDenseJSArray(dom_node_array);
  std::pair<uint8_t*, size_t> buffer_pair = serializer_->Release();

  CallNativeMethod(buffer_pair, "updateNode");
  return;
};

void HippyRenderManager::DeleteRenderNode(std::vector<std::shared_ptr<DomNode>>&& nodes) {
  std::shared_ptr<JNIEnvironment> instance = JNIEnvironment::GetInstance();
  JNIEnv* j_env = instance->AttachCurrentThread();

  jintArray j_int_array;
  j_int_array = j_env->NewIntArray(nodes.size());
  std::vector<int> id;
  id.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); i++) {
    id[i] = nodes[i]->GetId();
  }
  j_env->SetIntArrayRegion(j_int_array, 0, nodes.size(), &id[0]);

  jobject j_object = render_delegate_->GetObj();
  jclass j_class = j_env->GetObjectClass(j_object);
  if (!j_class) {
    TDF_BASE_LOG(ERROR) << "CallNativeMethod j_class error";
    return;
  }

  jmethodID j_method_id = j_env->GetMethodID(j_class, "deleteNode", "([I)V");
  if (!j_method_id) {
    TDF_BASE_LOG(ERROR) << "deleteNode j_cb_id error";
    return;
  }

  j_env->CallVoidMethod(j_object, j_method_id, j_int_array);
  j_env->DeleteLocalRef(j_int_array);
  return;
};

void HippyRenderManager::UpdateLayout(const std::vector<std::shared_ptr<DomNode>>& nodes) {
  serializer_->Release();
  serializer_->WriteHeader();

  uint32_t len = nodes.size();
  tdf::base::DomValue::DomValueArrayType dom_node_array;
  dom_node_array.resize(len);
  for (uint32_t i = 0; i < len; i++) {
    tdf::base::DomValue::DomValueObjectType dom_node;
    dom_node[kId] = tdf::base::DomValue(nodes[i]->GetId());
    auto result = nodes[i]->GetLayoutResult();
    dom_node[kWidth] = tdf::base::DomValue(DpToPx(result.width));
    dom_node[kHeight] = tdf::base::DomValue(DpToPx(result.height));
    dom_node[kLeft] = tdf::base::DomValue(DpToPx(result.left));
    dom_node[kTop] = tdf::base::DomValue(DpToPx(result.top));
    dom_node_array[i] = dom_node;
  }
  serializer_->WriteDenseJSArray(dom_node_array);
  std::pair<uint8_t*, size_t> buffer_pair = serializer_->Release();

  CallNativeMethod(buffer_pair, "updateLayout");
  return;
};

void HippyRenderManager::MoveRenderNode(std::vector<int32_t>&& moved_ids, int32_t from_pid, int32_t to_pid) {
  std::shared_ptr<JNIEnvironment> instance = JNIEnvironment::GetInstance();
  JNIEnv* j_env = instance->AttachCurrentThread();

  jintArray j_int_array;
  j_int_array = j_env->NewIntArray(moved_ids.size());
  j_env->SetIntArrayRegion(j_int_array, 0, moved_ids.size(), &moved_ids[0]);

  jobject j_object = render_delegate_->GetObj();
  jclass j_class = j_env->GetObjectClass(j_object);
  if (!j_class) {
    TDF_BASE_LOG(ERROR) << "CallNativeMethod j_class error";
    return;
  }

  jmethodID j_method_id = j_env->GetMethodID(j_class, "moveNode", "([III)V");
  if (!j_method_id) {
    TDF_BASE_LOG(ERROR) << "moveNode j_cb_id error";
    return;
  }

  j_env->CallVoidMethod(j_object, j_method_id, j_int_array, from_pid, to_pid);
  j_env->DeleteLocalRef(j_int_array);

  return;
};

void HippyRenderManager::Batch() {
  CallNativeMethod("endBatch");
  return;
};

void HippyRenderManager::AddEventListener(std::weak_ptr<DomNode> dom_node, const std::string& name) {
  TDF_BASE_NOTIMPLEMENTED();
}

void HippyRenderManager::RemoveEventListener(std::weak_ptr<DomNode> dom_node, const std::string& name) {
  TDF_BASE_NOTIMPLEMENTED();
}

void HippyRenderManager::CallFunction(std::weak_ptr<DomNode> domNode, const std::string& name, const DomArgument& param,
                                      CallFunctionCallback cb) {
  TDF_BASE_NOTIMPLEMENTED();
};

float HippyRenderManager::DpToPx(float dp) { return dp * density_; }

void HippyRenderManager::CallNativeMethod(const std::pair<uint8_t*, size_t>& buffer, const std::string& method) {
  std::shared_ptr<JNIEnvironment> instance = JNIEnvironment::GetInstance();
  JNIEnv* j_env = instance->AttachCurrentThread();

  jobject j_buffer;
  j_buffer = j_env->NewByteArray(buffer.second);
  j_env->SetByteArrayRegion(reinterpret_cast<jbyteArray>(j_buffer), 0, buffer.second,
                            reinterpret_cast<const jbyte*>(buffer.first));

  jobject j_object = render_delegate_->GetObj();
  jclass j_class = j_env->GetObjectClass(j_object);
  if (!j_class) {
    TDF_BASE_LOG(ERROR) << "CallNativeMethod j_class error";
    return;
  }

  jmethodID j_method_id = j_env->GetMethodID(j_class, method.c_str(), "([B)V");
  if (!j_method_id) {
    TDF_BASE_LOG(ERROR) << method << " j_method_id error";
    return;
  }

  j_env->CallVoidMethod(j_object, j_method_id, j_buffer);
  JNIEnvironment::ClearJEnvException(j_env);
  j_env->DeleteLocalRef(j_buffer);
}

void HippyRenderManager::CallNativeMethod(const std::string& method) {
  std::shared_ptr<JNIEnvironment> instance = JNIEnvironment::GetInstance();
  JNIEnv* j_env = instance->AttachCurrentThread();

  jobject j_object = render_delegate_->GetObj();
  jclass j_class = j_env->GetObjectClass(j_object);
  if (!j_class) {
    TDF_BASE_LOG(ERROR) << "CallNativeMethod j_class error";
    return;
  }

  jmethodID j_method_id = j_env->GetMethodID(j_class, method.c_str(), "()V");
  if (!j_method_id) {
    TDF_BASE_LOG(ERROR) << method << " j_method_id error";
    return;
  }

  j_env->CallVoidMethod(j_object, j_method_id);
  JNIEnvironment::ClearJEnvException(j_env);
}

void HippyRenderManager::CallNativeMeasureMethod(const int32_t id, const float width, const int32_t width_mode,
                                                 const float height, const int32_t height_mode,
                                                 const float left_padding, const float top_padding,
                                                 const float right_padding, const float bottom_padding,
                                                 int64_t& result) {
  std::shared_ptr<JNIEnvironment> instance = JNIEnvironment::GetInstance();
  JNIEnv* j_env = instance->AttachCurrentThread();

  jobject j_object = render_delegate_->GetObj();
  jclass j_class = j_env->GetObjectClass(j_object);
  if (!j_class) {
    TDF_BASE_LOG(ERROR) << "CallNativeMethod j_class error";
    return;
  }

  jmethodID j_method_id = j_env->GetMethodID(j_class, "measure", "(IFIFIFFFF)J");
  if (!j_method_id) {
    TDF_BASE_LOG(ERROR) << "measure j_method_id error";
    return;
  }

  jlong measure_result = j_env->CallLongMethod(j_object, j_method_id, id, width, width_mode, height, height_mode,
                                               left_padding, top_padding, right_padding, bottom_padding);
  JNIEnvironment::ClearJEnvException(j_env);

  result = (int64_t)measure_result;
}

}  // namespace dom
}  // namespace hippy