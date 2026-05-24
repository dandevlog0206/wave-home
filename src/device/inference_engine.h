#pragma once

#include <vector>
#include <string>
#include <array>
#include <memory>
#include <deque>

#define NET_NAMESPACE_BEGIN namespace net {
#define NET_NAMESPACE_END }

NET_NAMESPACE_BEGIN

class InferenceEngineImpl;

typedef size_t FrameIdx;
typedef size_t SequenceIdx;

constexpr FrameIdx FRAME_IDX_BACK = SIZE_MAX;
constexpr SequenceIdx SEQUENCE_IDX_BACK = SIZE_MAX;
constexpr uint32_t SEQUENCE_LENGTH_DEFAULT = 0;
constexpr size_t MAX_QUEUE_SIZE_SEQUENCE_LENGTH = 0;
constexpr size_t MAX_QUEUE_SIZE_NO_LIMIT = SIZE_MAX;

enum PipelineStageFlagBits
{
	PIPELINE_FRAME_ENCODER_BIT = 1 << 0,
	PIPELINE_TEMPORAL_AGGREGATOR_BIT = 1 << 1,
	PIPELINE_POST_PROCESSOR_BIT = 1 << 2
};

enum class CoordinateMode
{
	Cartesian,
	Polar
};

enum class PowerMode
{
	Linear,
	Decibel
};

struct Range
{
	float min;
	float max;
};

struct Point
{
	float x;
	float y;
	float z;
	float doppler;
	float power;
};

struct PipelineStageInfo
{
	std::string name;
	std::string version;
	PipelineStageFlagBits stage;
	std::string networkArchitecture;
	std::string paramPath;
	std::string binPath;
	std::string inputName;
	std::string outputName;
	uint32_t inputSize; // 0 for PIPELINE_FRAME_ENCODER_BIT
	uint32_t outputSize;
};

struct NormalizationInfo
{
	CoordinateMode coordinateMode;
	PowerMode powerMode;
	union
	{
		struct
		{
			Range rangeX;
			Range rangeY;
			Range rangeZ;
		};
		struct 
		{
			Range rangeR;
			Range rangeTheta;
			Range rangePhi;
		};
	};
	Range rangeDoppler;
	Range rangePower;
};

struct ModelInfo
{
	std::string name;
	std::string version;

	PipelineStageFlagBits pipelineStages;
	uint32_t sequenceLength;
	uint32_t classCount;
	uint32_t postProcessorOutputSize;

	NormalizationInfo normalizationInfo;

	PipelineStageInfo frameEncoderInfo;
	PipelineStageInfo temporalAggregatorInfo;
	PipelineStageInfo postProcessorInfo;

	uint32_t trainingSampleCount;
	uint32_t validationSampleCount;
	std::string optimizer;
	float learningRate;
	uint32_t batchSize;
	uint32_t accumulationStepCount;
	uint32_t epochCount;
	float trainingAccuracy;
	float validationAccuracy;
};

struct QueuePolicy
{
	// maximum number of frames to keep in the queue; if exceeded, the oldest frames will be dropped
	// if the value is less than the sequence length(MAX_QUEUE_SIZE_SEQUENCE_LENGTH), it will be automatically adjusted to be at least sequence_length
	// if the value is MAX_QUEUE_SIZE_NO_LIMIT, there will be no limit on the queue size (not recommended for long-running applications)
	size_t maxQueueSize = MAX_QUEUE_SIZE_SEQUENCE_LENGTH;
};

class InferenceEngine
{
public:
	InferenceEngine();
	InferenceEngine(const char* json_path);
	InferenceEngine(const InferenceEngine&) = delete;
	InferenceEngine(InferenceEngine&& rhs);
	~InferenceEngine();

	InferenceEngine& operator=(const InferenceEngine&) = delete;
	InferenceEngine& operator=(InferenceEngine&& rhs);

	// initialization
	void load(const char* json_path); // may throw exceptions on failure
	bool isLoaded() const;
	const ModelInfo& getModelInfo() const;

	void setQueuePolicy(const QueuePolicy& policy);
	const QueuePolicy& getQueuePolicy() const;

	// enqueue a frame for processing
	// if frameIdx is already in the queue, it will be updated with the new points
	// if frameIdx is FRAME_IDX_BACK, it will be treated as a new frame and pushed to the back of the queue
	void enqueueFrame(const std::vector<Point>& points, FrameIdx frame_idx = FRAME_IDX_BACK);
	/// Uses the first five floats of each Point (x,y,z,doppler,power) via reinterpret_cast.
	void enqueueFrame(const Point* points, size_t point_count, FrameIdx frame_idx = FRAME_IDX_BACK);
	size_t getQueuedFrameCount() const;
	bool hasFrame(FrameIdx frame_idx) const;
	
	bool hasSequence(SequenceIdx sequence_idx) const;
	size_t getSequenceCount() const;
	bool getSequenceFrameRange(SequenceIdx, FrameIdx& begin, FrameIdx& end) const;
	bool getNextRequiredFrameIndexForSequence(SequenceIdx sequence_idx, FrameIdx& out_frame_idx) const;

	const std::vector<float>& getFrameEmbedding(FrameIdx frame_idx) const;
	const std::vector<float>& getSequenceProbabilities(SequenceIdx sequence_idx = SEQUENCE_IDX_BACK) const;
	const std::vector<float>& getSequencePostProcessorOutput(SequenceIdx sequence_idx = SEQUENCE_IDX_BACK) const;
	const std::vector<float>& getSequenceEmbeddingMap(SequenceIdx sequence_idx = SEQUENCE_IDX_BACK, uint32_t sequence_length = SEQUENCE_LENGTH_DEFAULT) const;
	
	void clear();

private:
	// Configuration loading and validation
	void loadJsonConfig(const char* json_path);
	void validatePipeline();
	void loadModels();

private:
	std::unique_ptr<InferenceEngineImpl> m_impl;
};

NET_NAMESPACE_END
