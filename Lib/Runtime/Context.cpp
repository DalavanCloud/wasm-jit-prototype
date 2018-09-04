#include <string.h>
#include <atomic>
#include <vector>

#include "Inline/Assert.h"
#include "Inline/BasicTypes.h"
#include "Inline/Lock.h"
#include "Platform/Platform.h"
#include "Runtime/Runtime.h"
#include "Runtime/RuntimeData.h"
#include "RuntimePrivate.h"

using namespace Runtime;

Context* Runtime::createContext(Compartment* compartment)
{
	wavmAssert(compartment);
	Context* context = new Context(compartment);
	{
		Lock<Platform::Mutex> lock(compartment->mutex);

		// Allocate an ID for the context in the compartment.
		context->id = compartment->contexts.size();
		context->runtimeData = &compartment->runtimeData->contexts[context->id];
		compartment->contexts.push_back(context);

		// Commit the page(s) for the context's runtime data.
		errorUnless(Platform::commitVirtualPages(
			(U8*)context->runtimeData, sizeof(ContextRuntimeData) >> Platform::getPageSizeLog2()));

		// Initialize the context's global data.
		memcpy(context->runtimeData->globalData,
			   compartment->initialContextGlobalData,
			   compartment->numGlobalBytes);
	}

	return context;
}

void Runtime::Context::finalize()
{
	Lock<Platform::Mutex> compartmentLock(compartment->mutex);
	compartment->contexts[id] = nullptr;
}

Compartment* Runtime::getCompartmentFromContext(Context* context) { return context->compartment; }

Context* Runtime::cloneContext(Context* context, Compartment* newCompartment)
{
	// Create a new context and initialize its runtime data with the values from the source context.
	Context* clonedContext = createContext(newCompartment);
	const Uptr numGlobalBytes = context->compartment->numGlobalBytes;
	wavmAssert(numGlobalBytes <= newCompartment->numGlobalBytes);
	memcpy(
		clonedContext->runtimeData->globalData, context->runtimeData->globalData, numGlobalBytes);
	return clonedContext;
}