#include "inference_engine.h"
#include "retina.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <net.h>

NET_NAMESPACE_BEGIN

namespace
{
	constexpr float kPi = 3.14159265358979323846f;

	bool checkStageRole(const std::string& role)
	{
		return role == "frame_encoder" || role == "temporal_aggregator" || role == "post_processor";
	}

	PipelineStageFlagBits roleToFlag(const std::string& role)
	{
		if (role == "frame_encoder")
			return PIPELINE_FRAME_ENCODER_BIT;
		if (role == "temporal_aggregator")
			return PIPELINE_TEMPORAL_AGGREGATOR_BIT;
		return PIPELINE_POST_PROCESSOR_BIT;
	}

	void extractFlat(const ncnn::Mat& mat, uint32_t expected_size, std::vector<float>& out)
	{
		out.assign(expected_size, 0.f);
		const int total = mat.w * mat.h * mat.c * mat.d;
		const int copy_count = std::min(static_cast<int>(expected_size), total);
		const float* src = static_cast<const float*>(mat.data);
		if (src && copy_count > 0)
			std::memcpy(out.data(), src, static_cast<size_t>(copy_count) * sizeof(float));
	}

	void apply_softmax(std::vector<float>& logits)
	{
		if (logits.empty())
			return;

		float max_val = *std::max_element(logits.begin(), logits.end());
		float sum_exp = 0.0f;

		for (float& val : logits)
		{
			val = std::exp(val - max_val);
			sum_exp += val;
		}

		if (sum_exp > 0.0f)
		{
			for (float& val : logits)
				val /= sum_exp;
		}
	}

	Range parseRange(const nlohmann::json& arr, const char* name)
	{
		if (!arr.is_array() || arr.size() != 2)
			throw std::runtime_error(std::string("Invalid range for ") + name);
		return Range {arr[0].get<float>(), arr[1].get<float>()};
	}
}

struct NormalizedPoint
{
	union
	{
		struct
		{
			float x;
			float y;
			float z;
		};
		struct
		{
			float r;
			float theta;
			float phi;
		};
	};
	float doppler;
	float power;
};

struct Frame
{
	FrameIdx frameIndex = 0;
	std::vector<NormalizedPoint> points;
	std::vector<float> embedding;
	std::vector<float> probabilities;
	std::vector<float> postProcessed;
	bool encoderDone = false;
};

struct Sequence
{
	SequenceIdx sequenceIndex = 0;
	std::vector<FrameIdx> frameIndices;
	std::vector<float> probabilities;
	std::vector<float> postProcessed;
	bool aggregatorDone = false;
	bool postProcessorDone = false;
};

class InferenceEngineImpl
{
public:
	ModelInfo modelInfo {};
	QueuePolicy queuePolicy {};
	bool modelsLoaded = false;

	ncnn::Net frameEncoderNet;
	ncnn::Net temporalAggregatorNet;
	ncnn::Net postProcessorNet;

	ncnn::Mat frameEncoderInput;
	ncnn::Mat temporalAggregatorInput;
	ncnn::Mat postProcessorInput;

	std::deque<std::unique_ptr<Frame>> frameQueue;
	std::unordered_map<FrameIdx, Frame*> frameMap;

	std::unordered_map<SequenceIdx, std::unique_ptr<Sequence>> sequenceMap;

	FrameIdx nextAutoFrameIndex = 0;

	mutable std::vector<float> embeddingMap;
	mutable FrameIdx embeddingMapBegin = 0;
	mutable FrameIdx embeddingMapEnd = 0;
	mutable SequenceIdx embeddingMapSequence = SEQUENCE_IDX_BACK;
	mutable uint32_t embeddingMapLength = 0;
	mutable bool embeddingMapDirty = true;

	// --- config / models ---
	void loadJsonConfig(const char* json_path);
	void validatePipeline();
	void loadModels();
	void allocateWorkspaceMats();

	// --- frame path ---
	void normalizePoints(const std::vector<Point>& points, std::vector<NormalizedPoint>& out) const;
	Frame* upsertFrame(FrameIdx frame_index, const std::vector<Point>& points);
	void trimQueueIfNeeded();
	void refreshSequencesForEndpoint(FrameIdx end_index);
	void invalidateSequencesAffected(FrameIdx removed_index);

	bool computeSequenceWindow(FrameIdx end_index, FrameIdx& begin, FrameIdx& end) const;
	bool isWindowComplete(FrameIdx end_index) const;
	FrameIdx findFirstMissingFrame(FrameIdx end_index) const;

	void ensureFrameEncoder(FrameIdx frame_index);
	void ensureSequenceAggregated(SequenceIdx sequence_index);

	void runFrameEncoder(Frame& frame);
	void runTemporalAggregator(Sequence& sequence);
	void runPostProcessor(Sequence& sequence);

	SequenceIdx resolveSequenceIdx(SequenceIdx sequence_idx) const;
	Frame* resolveFrame(FrameIdx frame_idx) const;

	float normalizeAxisValue(float value, float min_val, float max_val) const;
	float normalizePowerValue(float power) const;
	std::array<float, 3> cartesianToPolar(float x, float y, float z) const;

	void updateEmbeddingMap(SequenceIdx sequence_idx, uint32_t sequence_length) const;
};

// =============================================================================
// InferenceEngineImpl — configuration
// =============================================================================

void InferenceEngineImpl::loadJsonConfig(const char* json_path)
{
	if (!json_path || !json_path[0])
		throw std::runtime_error("JSON path is empty");

	const std::filesystem::path config_path(json_path);
	if (!std::filesystem::exists(config_path))
		throw std::runtime_error(std::string("JSON file not found: ") + json_path);

	std::ifstream config_file(config_path);
	if (!config_file.is_open())
		throw std::runtime_error(std::string("Failed to open JSON file: ") + json_path);

	nlohmann::json json_config;
	try
	{
		config_file >> json_config;
	}
	catch (const nlohmann::json::exception& e)
	{
		throw std::runtime_error(std::string("JSON parse error: ") + e.what());
	}

	if (!json_config.contains("model_name"))
		throw std::runtime_error("Missing 'model_name' in JSON");
	if (!json_config.contains("model_version"))
		throw std::runtime_error("Missing 'model_version' in JSON");
	if (!json_config.contains("sequence_length"))
		throw std::runtime_error("Missing 'sequence_length' in JSON");
	if (!json_config.contains("normalization"))
		throw std::runtime_error("Missing 'normalization' in JSON");
	if (!json_config.contains("pipeline"))
		throw std::runtime_error("Missing 'pipeline' in JSON");

	modelInfo = ModelInfo {};
	modelInfo.name = json_config["model_name"].get<std::string>();
	modelInfo.version = json_config["model_version"].get<std::string>();
	modelInfo.sequenceLength = json_config["sequence_length"].get<uint32_t>();
	if (modelInfo.sequenceLength == 0)
		throw std::runtime_error("sequence_length must be greater than 0");

	const auto& norm = json_config["normalization"];
	const std::string coordinate_mode = norm["coordinate_mode"].get<std::string>();
	if (coordinate_mode == "cartesian")
		modelInfo.normalizationInfo.coordinateMode = CoordinateMode::Cartesian;
	else if (coordinate_mode == "polar")
		modelInfo.normalizationInfo.coordinateMode = CoordinateMode::Polar;
	else
		throw std::runtime_error("coordinate_mode must be 'cartesian' or 'polar'");

	const std::string power_mode = norm["power_mode"].get<std::string>();
	if (power_mode == "linear")
		modelInfo.normalizationInfo.powerMode = PowerMode::Linear;
	else if (power_mode == "db")
		modelInfo.normalizationInfo.powerMode = PowerMode::Decibel;
	else
		throw std::runtime_error("power_mode must be 'linear' or 'db'");

	const auto& ranges = norm["ranges"];
	modelInfo.normalizationInfo.rangeX = parseRange(ranges["x"], "x");
	modelInfo.normalizationInfo.rangeY = parseRange(ranges["y"], "y");
	modelInfo.normalizationInfo.rangeZ = parseRange(ranges["z"], "z");
	modelInfo.normalizationInfo.rangeDoppler = parseRange(ranges["doppler"], "doppler");
	modelInfo.normalizationInfo.rangePower = parseRange(ranges["power"], "power");

	const auto& pipeline = json_config["pipeline"];
	if (!pipeline.is_array() || pipeline.empty())
		throw std::runtime_error("pipeline must be a non-empty array");

	const std::filesystem::path config_dir = config_path.parent_path();
	uint32_t prev_output_size = 0;

	for (const auto& stage : pipeline)
	{
		PipelineStageInfo info {};
		info.name = stage.at("name").get<std::string>();
		info.version = stage.value("version", "1.0");
		const std::string role = stage.at("role").get<std::string>();
		if (!checkStageRole(role))
			throw std::runtime_error("Invalid pipeline role: " + role);

		info.stage = roleToFlag(role);
		info.networkArchitecture = stage.at("type").get<std::string>();
		info.paramPath = (config_dir / stage.at("param").get<std::string>()).string();
		info.binPath = (config_dir / stage.at("bin").get<std::string>()).string();
		info.inputName = stage.value("input_name", "in0");
		info.outputName = stage.value("output_name", "out0");
		info.inputSize = prev_output_size;
		info.outputSize = stage.at("output_size").get<uint32_t>();
		prev_output_size = info.outputSize;

		if (info.stage == PIPELINE_FRAME_ENCODER_BIT)
			modelInfo.frameEncoderInfo = info;
		else if (info.stage == PIPELINE_TEMPORAL_AGGREGATOR_BIT)
			modelInfo.temporalAggregatorInfo = info;
		else if (info.stage == PIPELINE_POST_PROCESSOR_BIT)
			modelInfo.postProcessorInfo = info;

		modelInfo.pipelineStages =
			static_cast<PipelineStageFlagBits>(modelInfo.pipelineStages | info.stage);
	}

	if (json_config.contains("training"))
	{
		const auto& training = json_config["training"];
		modelInfo.optimizer = training.value("name", "");
		modelInfo.learningRate = training.value("learning_rate", 0.f);
		modelInfo.batchSize = training.value("batch_size", 0u);
		modelInfo.accumulationStepCount = training.value("accumulation_steps", 0u);
		modelInfo.epochCount = training.value("num_epochs", 0u);
		modelInfo.trainingAccuracy = training.value("training_accuracy", 0.f);
		modelInfo.validationAccuracy = training.value("validation_accuracy", 0.f);
		modelInfo.validationSampleCount =
			static_cast<uint32_t>(training.value("validation_split", 0.f) * 1000.f);
	}

	modelInfo.classCount = prev_output_size;
	modelInfo.postProcessorOutputSize =
		(modelInfo.pipelineStages & PIPELINE_POST_PROCESSOR_BIT)
			? modelInfo.postProcessorInfo.outputSize
			: 0u;
}

void InferenceEngineImpl::validatePipeline()
{
	if (!(modelInfo.pipelineStages & PIPELINE_FRAME_ENCODER_BIT))
		throw std::runtime_error("Pipeline must include frame_encoder");

	const bool has_aggregator =
		(modelInfo.pipelineStages & PIPELINE_TEMPORAL_AGGREGATOR_BIT) != 0;
	const bool has_post = (modelInfo.pipelineStages & PIPELINE_POST_PROCESSOR_BIT) != 0;

	if (has_post && !has_aggregator)
		throw std::runtime_error("post_processor requires temporal_aggregator");

	if (has_aggregator)
		modelInfo.classCount = modelInfo.temporalAggregatorInfo.outputSize;
	else
		modelInfo.classCount = modelInfo.frameEncoderInfo.outputSize;

	if (has_post)
		modelInfo.classCount = modelInfo.postProcessorInfo.outputSize;
}

void InferenceEngineImpl::loadModels()
{
	auto load_net = [](ncnn::Net& net, const PipelineStageInfo& info) {
		if (!std::filesystem::exists(info.paramPath))
			throw std::runtime_error("Model param file not found: " + info.paramPath);
		if (!std::filesystem::exists(info.binPath))
			throw std::runtime_error("Model bin file not found: " + info.binPath);
		if (net.load_param(info.paramPath.c_str()) != 0)
			throw std::runtime_error("Failed to load param: " + info.paramPath);
		if (net.load_model(info.binPath.c_str()) != 0)
			throw std::runtime_error("Failed to load bin: " + info.binPath);
	};

	load_net(frameEncoderNet, modelInfo.frameEncoderInfo);

	if (modelInfo.pipelineStages & PIPELINE_TEMPORAL_AGGREGATOR_BIT)
		load_net(temporalAggregatorNet, modelInfo.temporalAggregatorInfo);

	if (modelInfo.pipelineStages & PIPELINE_POST_PROCESSOR_BIT)
		load_net(postProcessorNet, modelInfo.postProcessorInfo);

	modelsLoaded = true;
	allocateWorkspaceMats();
}

void InferenceEngineImpl::allocateWorkspaceMats()
{
	const uint32_t embed = modelInfo.frameEncoderInfo.outputSize;
	const uint32_t seq = modelInfo.sequenceLength;

	// LSTM input: w = embedding dim, h = time steps (one row per frame).
	temporalAggregatorInput.create(static_cast<int>(embed), static_cast<int>(seq));
}

// =============================================================================
// InferenceEngineImpl — normalization
// =============================================================================

float InferenceEngineImpl::normalizeAxisValue(float value, float min_val, float max_val) const
{
	if (max_val <= min_val)
		return 0.f;
	return (value - min_val) / (max_val - min_val) * 2.f - 1.f;
}

float InferenceEngineImpl::normalizePowerValue(float power) const
{
	float v = power;
	if (modelInfo.normalizationInfo.powerMode == PowerMode::Decibel)
		v = 10.f * std::log10(power + 1.f);
	return normalizeAxisValue(v,
		modelInfo.normalizationInfo.rangePower.min,
		modelInfo.normalizationInfo.rangePower.max);
}

std::array<float, 3> InferenceEngineImpl::cartesianToPolar(float x, float y, float z) const
{
	const float r = std::sqrt(x * x + y * y + z * z);
	const float theta = std::atan2(y, x);
	const float phi = std::atan2(z, std::sqrt(x * x + y * y));
	return {r, theta, phi};
}

void InferenceEngineImpl::normalizePoints(
	const std::vector<Point>& points,
	std::vector<NormalizedPoint>& out) const
{
	out.clear();
	out.reserve(points.size());

	const auto& n = modelInfo.normalizationInfo;

	for (const auto& p : points)
	{
		NormalizedPoint np {};
		if (n.coordinateMode == CoordinateMode::Polar)
		{
			const auto [r, theta, phi] = cartesianToPolar(p.x, p.y, p.z);
			const float max_range = std::max({
				n.rangeX.max - n.rangeX.min,
				n.rangeY.max - n.rangeY.min,
				n.rangeZ.max - n.rangeZ.min,
			});
			np.r = normalizeAxisValue(r, 0.f, max_range);
			np.theta = normalizeAxisValue(theta, -kPi, kPi);
			np.phi = normalizeAxisValue(phi, -kPi * 0.5f, kPi * 0.5f);
		}
		else
		{
			np.x = normalizeAxisValue(p.x, n.rangeX.min, n.rangeX.max);
			np.y = normalizeAxisValue(p.y, n.rangeY.min, n.rangeY.max);
			np.z = normalizeAxisValue(p.z, n.rangeZ.min, n.rangeZ.max);
		}
		np.doppler = normalizeAxisValue(p.doppler, n.rangeDoppler.min, n.rangeDoppler.max);
		np.power = normalizePowerValue(p.power);
		out.push_back(np);
	}
}

// =============================================================================
// InferenceEngineImpl — queue / sequences
// =============================================================================

bool InferenceEngineImpl::computeSequenceWindow(
	FrameIdx end_index,
	FrameIdx& begin,
	FrameIdx& end) const
{
	end = end_index;
	if (end_index + 1 < modelInfo.sequenceLength)
	{
		begin = 0;
		return false;
	}
	begin = end_index + 1 - modelInfo.sequenceLength;
	return true;
}

bool InferenceEngineImpl::isWindowComplete(FrameIdx end_index) const
{
	FrameIdx begin = 0;
	FrameIdx end = 0;
	if (!computeSequenceWindow(end_index, begin, end))
		return false;
	for (FrameIdx i = begin; i <= end; ++i)
	{
		if (frameMap.find(i) == frameMap.end())
			return false;
	}
	return true;
}

FrameIdx InferenceEngineImpl::findFirstMissingFrame(FrameIdx end_index) const
{
	FrameIdx begin = 0;
	FrameIdx end = 0;
	if (!computeSequenceWindow(end_index, begin, end))
	{
		for (FrameIdx i = 0; i <= end_index; ++i)
		{
			if (frameMap.find(i) == frameMap.end())
				return i;
		}
		return end_index + 1;
	}
	for (FrameIdx i = begin; i <= end; ++i)
	{
		if (frameMap.find(i) == frameMap.end())
			return i;
	}
	return end + 1;
}

Frame* InferenceEngineImpl::upsertFrame(FrameIdx frame_index, const std::vector<Point>& points)
{
	Frame* frame = nullptr;
	const auto it = frameMap.find(frame_index);
	if (it != frameMap.end())
	{
		frame = it->second;
	}
	else
	{
		auto owned = std::make_unique<Frame>();
		frame = owned.get();
		frame->frameIndex = frame_index;
		frameQueue.push_back(std::move(owned));
		frameMap[frame_index] = frame;
	}

	normalizePoints(points, frame->points);
	frame->encoderDone = false;
	frame->embedding.clear();
	embeddingMapDirty = true;

	// Drop sequence results that depend on this endpoint or may change.
	for (auto& [seq_idx, seq] : sequenceMap)
	{
		if (seq_idx >= frame_index - modelInfo.sequenceLength + 1 && seq_idx <= frame_index)
		{
			seq->aggregatorDone = false;
			seq->postProcessorDone = false;
			seq->probabilities.clear();
			seq->postProcessed.clear();
		}
	}

	return frame;
}

void InferenceEngineImpl::trimQueueIfNeeded()
{
	size_t max_size = queuePolicy.maxQueueSize;
	if (max_size == MAX_QUEUE_SIZE_SEQUENCE_LENGTH)
		max_size = modelInfo.sequenceLength;
	else if (max_size < modelInfo.sequenceLength)
		max_size = modelInfo.sequenceLength;

	if (max_size == MAX_QUEUE_SIZE_NO_LIMIT)
		return;

	while (frameMap.size() > max_size)
	{
		const FrameIdx oldest = frameQueue.front()->frameIndex;
		invalidateSequencesAffected(oldest);
		frameMap.erase(oldest);
		frameQueue.pop_front();
		embeddingMapDirty = true;
	}
}

void InferenceEngineImpl::invalidateSequencesAffected(FrameIdx removed_index)
{
	std::vector<SequenceIdx> to_erase;
	for (const auto& [seq_idx, seq] : sequenceMap)
	{
		FrameIdx begin = 0;
		FrameIdx end = 0;
		if (!computeSequenceWindow(seq_idx, begin, end))
			continue;
		if (removed_index >= begin && removed_index <= end)
			to_erase.push_back(seq_idx);
	}
	for (SequenceIdx idx : to_erase)
		sequenceMap.erase(idx);
}

void InferenceEngineImpl::refreshSequencesForEndpoint(FrameIdx end_index)
{
	if (!isWindowComplete(end_index))
		return;

	std::unique_ptr<Sequence>& seq = sequenceMap[end_index];
	if (!seq)
	{
		seq = std::make_unique<Sequence>();
		seq->sequenceIndex = end_index;
	}

	FrameIdx begin = 0;
	FrameIdx end = 0;
	computeSequenceWindow(end_index, begin, end);

	seq->frameIndices.clear();
	seq->frameIndices.reserve(modelInfo.sequenceLength);
	for (FrameIdx i = begin; i <= end; ++i)
		seq->frameIndices.push_back(i);

	ensureSequenceAggregated(end_index);
}

Frame* InferenceEngineImpl::resolveFrame(FrameIdx frame_idx) const
{
	if (frame_idx == FRAME_IDX_BACK)
	{
		if (frameQueue.empty())
			return nullptr;
		return frameQueue.back().get();
	}
	const auto it = frameMap.find(frame_idx);
	return it != frameMap.end() ? it->second : nullptr;
}

SequenceIdx InferenceEngineImpl::resolveSequenceIdx(SequenceIdx sequence_idx) const
{
	if (sequence_idx != SEQUENCE_IDX_BACK)
		return sequence_idx;

	SequenceIdx best = SEQUENCE_IDX_BACK;
	for (const auto& [idx, seq] : sequenceMap)
	{
		if (!seq->aggregatorDone)
			continue;
		if (best == SEQUENCE_IDX_BACK || idx > best)
			best = idx;
	}
	return best;
}

// =============================================================================
// InferenceEngineImpl — ncnn inference (cached stages)
// =============================================================================

void InferenceEngineImpl::ensureFrameEncoder(FrameIdx frame_index)
{
	Frame* frame = resolveFrame(frame_index);
	if (!frame)
		throw std::runtime_error("Frame not found for encoder");
	if (frame->encoderDone)
		return;
	runFrameEncoder(*frame);
}

void InferenceEngineImpl::ensureSequenceAggregated(SequenceIdx sequence_index)
{
	const SequenceIdx resolved = resolveSequenceIdx(sequence_index);
	auto it = sequenceMap.find(resolved);
	if (it == sequenceMap.end() || !it->second)
		throw std::runtime_error("Sequence not found");

	Sequence& sequence = *it->second;
	if (sequence.aggregatorDone)
		return;

	for (FrameIdx idx : sequence.frameIndices)
		ensureFrameEncoder(idx);

	if (modelInfo.pipelineStages & PIPELINE_TEMPORAL_AGGREGATOR_BIT)
		runTemporalAggregator(sequence);
	else
	{
		// Encoder-only pipeline: expose last frame embedding as output.
		Frame* last = frameMap.at(sequence.frameIndices.back());
		sequence.probabilities = last->embedding;
		sequence.aggregatorDone = true;
	}

	if (modelInfo.pipelineStages & PIPELINE_POST_PROCESSOR_BIT)
		runPostProcessor(sequence);
	else
		sequence.postProcessorDone = true;

	embeddingMapDirty = true;
}

void InferenceEngineImpl::runFrameEncoder(Frame& frame)
{
	const int point_count = static_cast<int>(frame.points.size());
	const uint32_t embed_size = modelInfo.frameEncoderInfo.outputSize;

	if (point_count == 0)
	{
		frame.embedding.assign(embed_size, 0.f);
		frame.encoderDone = true;
		return;
	}

	// PointNet Conv1D input layout: w = num_points, h = 5 channels.
	// row(channel)[point_index]
	frameEncoderInput.create(point_count, 5);
	for (int i = 0; i < point_count; ++i)
	{
		const NormalizedPoint& p = frame.points[static_cast<size_t>(i)];
		if (modelInfo.normalizationInfo.coordinateMode == CoordinateMode::Polar)
		{
			frameEncoderInput.row(0)[i] = p.r;
			frameEncoderInput.row(1)[i] = p.theta;
			frameEncoderInput.row(2)[i] = p.phi;
		}
		else
		{
			frameEncoderInput.row(0)[i] = p.x;
			frameEncoderInput.row(1)[i] = p.y;
			frameEncoderInput.row(2)[i] = p.z;
		}
		frameEncoderInput.row(3)[i] = p.doppler;
		frameEncoderInput.row(4)[i] = p.power;
	}

	ncnn::Extractor ex = frameEncoderNet.create_extractor();
	const auto& info = modelInfo.frameEncoderInfo;
	ex.input(info.inputName.c_str(), frameEncoderInput);

	ncnn::Mat output;
	if (ex.extract(info.outputName.c_str(), output) != 0)
		throw std::runtime_error("frame_encoder extract failed");

	extractFlat(output, embed_size, frame.embedding);
	frame.encoderDone = true;
}

void InferenceEngineImpl::runTemporalAggregator(Sequence& sequence)
{
	const auto& agg_info = modelInfo.temporalAggregatorInfo;
	const uint32_t embed_size = modelInfo.frameEncoderInfo.outputSize;
	const int seq_len = static_cast<int>(sequence.frameIndices.size());

	if (seq_len != static_cast<int>(modelInfo.sequenceLength))
		throw std::runtime_error("Sequence length mismatch for temporal aggregator");

	// w = embedding_size, h = seq_len; row t = embedding at frameIndices[t]
	temporalAggregatorInput.create(static_cast<int>(embed_size), seq_len);
	for (int t = 0; t < seq_len; ++t)
	{
		Frame* frame = frameMap.at(sequence.frameIndices[static_cast<size_t>(t)]);
		if (!frame->encoderDone)
			throw std::runtime_error("Missing cached embedding before aggregator");
		float* row = temporalAggregatorInput.row(t);
		std::memcpy(row,
			frame->embedding.data(),
			static_cast<size_t>(embed_size) * sizeof(float));
	}

	ncnn::Extractor ex = temporalAggregatorNet.create_extractor();
	ex.input(agg_info.inputName.c_str(), temporalAggregatorInput);

	ncnn::Mat output;
	if (ex.extract(agg_info.outputName.c_str(), output) != 0)
		throw std::runtime_error("temporal_aggregator extract failed");

	extractFlat(output, agg_info.outputSize, sequence.probabilities);
	apply_softmax(sequence.probabilities);
	sequence.aggregatorDone = true;
}

void InferenceEngineImpl::runPostProcessor(Sequence& sequence)
{
	const auto& pp_info = modelInfo.postProcessorInfo;
	const std::vector<float>& source =
		sequence.probabilities.empty() ? sequence.postProcessed : sequence.probabilities;

	postProcessorInput.create(static_cast<int>(pp_info.inputSize), 1);
	if (pp_info.inputSize > 0)
	{
		const size_t n = std::min(static_cast<size_t>(pp_info.inputSize), source.size());
		std::memcpy(postProcessorInput.data, source.data(), n * sizeof(float));
	}

	ncnn::Extractor ex = postProcessorNet.create_extractor();
	ex.input(pp_info.inputName.c_str(), postProcessorInput);

	ncnn::Mat output;
	if (ex.extract(pp_info.outputName.c_str(), output) != 0)
		throw std::runtime_error("post_processor extract failed");

	extractFlat(output, pp_info.outputSize, sequence.postProcessed);
	sequence.postProcessorDone = true;
}

void InferenceEngineImpl::updateEmbeddingMap(SequenceIdx sequence_idx, uint32_t sequence_length) const
{
	const SequenceIdx resolved = resolveSequenceIdx(sequence_idx);
	auto it = sequenceMap.find(resolved);
	if (it == sequenceMap.end() || !it->second || it->second->frameIndices.empty())
	{
		embeddingMap.clear();
		return;
	}

	const Sequence& sequence = *it->second;
	uint32_t len = sequence_length;
	if (len == SEQUENCE_LENGTH_DEFAULT)
		len = modelInfo.sequenceLength;
	len = std::min(len, static_cast<uint32_t>(sequence.frameIndices.size()));

	const uint32_t embed_size = modelInfo.frameEncoderInfo.outputSize;
	embeddingMap.assign(static_cast<size_t>(len) * embed_size, 0.f);

	for (uint32_t t = 0; t < len; ++t)
	{
		Frame* frame = frameMap.at(sequence.frameIndices[t]);
		if (!frame->encoderDone)
			continue;
		std::memcpy(embeddingMap.data() + static_cast<size_t>(t) * embed_size,
			frame->embedding.data(),
			static_cast<size_t>(embed_size) * sizeof(float));
	}

	embeddingMapBegin = sequence.frameIndices.front();
	embeddingMapEnd = sequence.frameIndices[len - 1];
	embeddingMapSequence = resolved;
	embeddingMapLength = len;
	embeddingMapDirty = false;
}

// =============================================================================
// InferenceEngine — public API
// =============================================================================

InferenceEngine::InferenceEngine() :
	m_impl(nullptr)
{
}

InferenceEngine::InferenceEngine(const char* json_path) :
	InferenceEngine()
{
	load(json_path);
}

InferenceEngine::InferenceEngine(InferenceEngine&& rhs) :
	m_impl(std::move(rhs.m_impl))
{
}

InferenceEngine::~InferenceEngine() = default;

InferenceEngine& InferenceEngine::operator=(InferenceEngine&& rhs)
{
	if (this != &rhs)
		m_impl = std::move(rhs.m_impl);
	return *this;
}

void InferenceEngine::load(const char* json_path)
{
	try
	{
		auto impl = std::make_unique<InferenceEngineImpl>();
		impl->loadJsonConfig(json_path);
		impl->validatePipeline();
		impl->loadModels();
		m_impl = std::move(impl);
	}
	catch (...)
	{
		m_impl.reset();
		throw;
	}
}

bool InferenceEngine::isLoaded() const
{
	return m_impl && m_impl->modelsLoaded;
}

const ModelInfo& InferenceEngine::getModelInfo() const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	return m_impl->modelInfo;
}

void InferenceEngine::setQueuePolicy(const QueuePolicy& policy)
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	m_impl->queuePolicy = policy;
}

const QueuePolicy& InferenceEngine::getQueuePolicy() const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	return m_impl->queuePolicy;
}

void InferenceEngine::enqueueFrame(const std::vector<Point>& points, FrameIdx frame_idx)
{
	if (!m_impl || !m_impl->modelsLoaded)
		throw std::runtime_error("InferenceEngine not loaded");

	FrameIdx index = frame_idx;
	if (index == FRAME_IDX_BACK)
		index = m_impl->nextAutoFrameIndex++;

	Frame* frame = m_impl->upsertFrame(index, points);
	(void)frame;

	m_impl->ensureFrameEncoder(index);

	// Any endpoint whose window includes this frame may have become complete.
	const FrameIdx last_endpoint = index + m_impl->modelInfo.sequenceLength - 1;
	for (FrameIdx end = index; end <= last_endpoint; ++end)
		m_impl->refreshSequencesForEndpoint(end);

	m_impl->trimQueueIfNeeded();
}

void InferenceEngine::enqueueFrame(std::vector<Point>&& points, FrameIdx frame_idx)
{
	enqueueFrame(points, frame_idx);
}

void InferenceEngine::enqueueFrame(const Point* points, const size_t point_count, FrameIdx frame_idx)
{
	std::vector<Point> vec(points, points + point_count);
	enqueueFrame(std::move(vec), frame_idx);
}

size_t InferenceEngine::getQueuedFrameCount() const
{
	if (!m_impl)
		return 0;
	return m_impl->frameMap.size();
}

bool InferenceEngine::hasFrame(FrameIdx frame_idx) const
{
	if (!m_impl)
		return false;
	if (frame_idx == FRAME_IDX_BACK)
		return !m_impl->frameQueue.empty();
	return m_impl->frameMap.find(frame_idx) != m_impl->frameMap.end();
}

bool InferenceEngine::hasSequence(SequenceIdx sequence_idx) const
{
	if (!m_impl)
		return false;
	const SequenceIdx resolved = m_impl->resolveSequenceIdx(sequence_idx);
	if (resolved == SEQUENCE_IDX_BACK)
		return false;
	const auto it = m_impl->sequenceMap.find(resolved);
	return it != m_impl->sequenceMap.end() && it->second && it->second->aggregatorDone;
}

size_t InferenceEngine::getSequenceCount() const
{
	if (!m_impl)
		return 0;
	return m_impl->sequenceMap.size();
}

bool InferenceEngine::getSequenceFrameRange(
	SequenceIdx sequence_idx,
	FrameIdx& begin,
	FrameIdx& end) const
{
	if (!m_impl)
		return false;
	const SequenceIdx resolved = m_impl->resolveSequenceIdx(sequence_idx);
	const auto it = m_impl->sequenceMap.find(resolved);
	if (it == m_impl->sequenceMap.end() || !it->second || it->second->frameIndices.empty())
		return false;
	begin = it->second->frameIndices.front();
	end = it->second->frameIndices.back();
	return true;
}

bool InferenceEngine::getNextRequiredFrameIndexForSequence(
	SequenceIdx sequence_idx,
	FrameIdx& out_frame_idx) const
{
	if (!m_impl)
		return false;

	SequenceIdx end_index = sequence_idx;
	if (end_index == SEQUENCE_IDX_BACK)
	{
		if (m_impl->frameQueue.empty())
			return false;
		end_index = m_impl->frameQueue.back()->frameIndex;
	}

	const FrameIdx missing = m_impl->findFirstMissingFrame(end_index);
	FrameIdx begin = 0;
	FrameIdx end = 0;
	if (!m_impl->computeSequenceWindow(end_index, begin, end))
	{
		out_frame_idx = missing;
		return true;
	}
	if (missing > end)
		return false;
	out_frame_idx = missing;
	return true;
}

const std::vector<float>& InferenceEngine::getFrameEmbedding(FrameIdx frame_idx) const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	Frame* frame = m_impl->resolveFrame(frame_idx);
	if (!frame)
		throw std::runtime_error("Frame not found");
	if (!frame->encoderDone)
		throw std::runtime_error("Frame embedding not computed");
	return frame->embedding;
}

const std::vector<float>& InferenceEngine::getSequenceProbabilities(
	SequenceIdx sequence_idx) const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	const SequenceIdx resolved = m_impl->resolveSequenceIdx(sequence_idx);
	const auto it = m_impl->sequenceMap.find(resolved);
	if (it == m_impl->sequenceMap.end() || !it->second || !it->second->aggregatorDone)
		throw std::runtime_error("Sequence probabilities not available");
	return it->second->probabilities;
}

const std::vector<float>& InferenceEngine::getSequencePostProcessorOutput(
	SequenceIdx sequence_idx) const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	if (!(m_impl->modelInfo.pipelineStages & PIPELINE_POST_PROCESSOR_BIT))
		throw std::runtime_error("Pipeline has no post_processor stage");
	const SequenceIdx resolved = m_impl->resolveSequenceIdx(sequence_idx);
	const auto it = m_impl->sequenceMap.find(resolved);
	if (it == m_impl->sequenceMap.end() || !it->second || !it->second->postProcessorDone)
		throw std::runtime_error("Post-processor output not available");
	return it->second->postProcessed;
}

const std::vector<float>& InferenceEngine::getSequenceEmbeddingMap(
	SequenceIdx sequence_idx,
	uint32_t sequence_length) const
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	const SequenceIdx resolved = m_impl->resolveSequenceIdx(sequence_idx);
	if (m_impl->embeddingMapDirty || m_impl->embeddingMapSequence != resolved)
		m_impl->updateEmbeddingMap(resolved, sequence_length);
	return m_impl->embeddingMap;
}

void InferenceEngine::clear()
{
	if (!m_impl)
		return;
	m_impl->frameQueue.clear();
	m_impl->frameMap.clear();
	m_impl->sequenceMap.clear();
	m_impl->nextAutoFrameIndex = 0;
	m_impl->embeddingMap.clear();
	m_impl->embeddingMapDirty = true;
}

void InferenceEngine::loadJsonConfig(const char* json_path)
{
	if (!m_impl)
		m_impl = std::make_unique<InferenceEngineImpl>();
	m_impl->loadJsonConfig(json_path);
}

void InferenceEngine::validatePipeline()
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	m_impl->validatePipeline();
}

void InferenceEngine::loadModels()
{
	if (!m_impl)
		throw std::runtime_error("InferenceEngine not loaded");
	m_impl->loadModels();
}

NET_NAMESPACE_END
