#include "Channelizer.hxx"
#include <CLAM/ProcessingFactory.hxx>
static const char * metadata[] = {
	"key", "Channelizer",
	"category", "Analysis",
	0
	};
static CLAM::FactoryRegistrator<CLAM::ProcessingFactory, CLAM::Channelizer> registrator(metadata);
