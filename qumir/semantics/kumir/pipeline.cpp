#include "pipeline.h"

namespace NQumir {
namespace NSemantics {
namespace NKumir {

NTransform::TPipelineExtensions PipelineExtensions() {
    NTransform::TPipelineExtensions extensions;
    extensions.AfterTypeAnnotation.push_back(
        NTransform::CoroutineAnnotationTransform);
    return extensions;
}

} // namespace NKumir
} // namespace NSemantics
} // namespace NQumir
