/**
 * VitaABS - Audio Utilities Implementation
 */

#include "utils/audio_utils.hpp"
#include <borealis.hpp>
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

namespace vitaabs {

bool concatenateAudioFiles(const std::vector<std::string>& inputFiles,
                           const std::string& outputPath,
                           std::function<void(int, int)> progressCallback) {
    if (inputFiles.empty()) {
        brls::Logger::error("concatenateAudioFiles: No input files provided");
        return false;
    }

    // If only one file, just return success (no need to concatenate)
    if (inputFiles.size() == 1) {
        brls::Logger::info("concatenateAudioFiles: Only one file, no concatenation needed");
        return true;
    }

    brls::Logger::info("concatenateAudioFiles: Combining {} files into {}", inputFiles.size(), outputPath);

    // Create a concat file list for the concat demuxer
    std::string concatListPath = outputPath + ".concat.txt";

#ifdef __vita__
    SceUID listFd = sceIoOpen(concatListPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (listFd >= 0) {
        for (const auto& file : inputFiles) {
            std::string line = "file '" + file + "'\n";
            sceIoWrite(listFd, line.c_str(), line.size());
        }
        sceIoClose(listFd);
    } else {
        brls::Logger::error("concatenateAudioFiles: Failed to create concat list file");
        return false;
    }
#else
    std::ofstream listFile(concatListPath);
    if (listFile.is_open()) {
        for (const auto& file : inputFiles) {
            listFile << "file '" << file << "'\n";
        }
        listFile.close();
    } else {
        brls::Logger::error("concatenateAudioFiles: Failed to create concat list file");
        return false;
    }
#endif

    // Open input using concat demuxer
    AVFormatContext* inputFmtCtx = nullptr;
    AVDictionary* options = nullptr;

    // Set safe option for concat demuxer to allow absolute paths
    av_dict_set(&options, "safe", "0", 0);

    // Use the concat demuxer format
    const AVInputFormat* concatFmt = av_find_input_format("concat");
    if (!concatFmt) {
        brls::Logger::error("concatenateAudioFiles: concat demuxer not available");
        av_dict_free(&options);
        return false;
    }

    int ret = avformat_open_input(&inputFmtCtx, concatListPath.c_str(), concatFmt, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        brls::Logger::error("concatenateAudioFiles: Could not open input: {}", errbuf);
        return false;
    }

    ret = avformat_find_stream_info(inputFmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not find stream info");
        avformat_close_input(&inputFmtCtx);
        return false;
    }

    // Create output format context
    AVFormatContext* outputFmtCtx = nullptr;

    // Determine output format based on extension
    std::string outputExt = ".m4b";
    size_t dotPos = outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        outputExt = outputPath.substr(dotPos);
    }

    const char* outputFormat = "ipod";  // For m4b/m4a
    if (outputExt == ".mp3") {
        outputFormat = "mp3";
    } else if (outputExt == ".ogg") {
        outputFormat = "ogg";
    }

    ret = avformat_alloc_output_context2(&outputFmtCtx, nullptr, outputFormat, outputPath.c_str());
    if (ret < 0 || !outputFmtCtx) {
        brls::Logger::error("concatenateAudioFiles: Could not create output context");
        avformat_close_input(&inputFmtCtx);
        return false;
    }

    // Copy streams from input to output
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < inputFmtCtx->nb_streams; i++) {
        AVStream* inStream = inputFmtCtx->streams[i];
        if (inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVStream* outStream = avformat_new_stream(outputFmtCtx, nullptr);
            if (!outStream) {
                brls::Logger::error("concatenateAudioFiles: Could not create output stream");
                avformat_close_input(&inputFmtCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
            if (ret < 0) {
                brls::Logger::error("concatenateAudioFiles: Could not copy codec parameters");
                avformat_close_input(&inputFmtCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            outStream->codecpar->codec_tag = 0;
            audioStreamIndex = i;
            break;
        }
    }

    if (audioStreamIndex < 0) {
        brls::Logger::error("concatenateAudioFiles: No audio stream found");
        avformat_close_input(&inputFmtCtx);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Open output file
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFmtCtx->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            brls::Logger::error("concatenateAudioFiles: Could not open output file: {}", errbuf);
            avformat_close_input(&inputFmtCtx);
            avformat_free_context(outputFmtCtx);
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(outputFmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not write header");
        avformat_close_input(&inputFmtCtx);
        if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFmtCtx->pb);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Copy packets
    AVPacket* pkt = av_packet_alloc();
    int64_t packetsWritten = 0;
    int64_t lastProgressReport = 0;

    while (av_read_frame(inputFmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioStreamIndex) {
            // Rescale timestamps
            AVStream* inStream = inputFmtCtx->streams[audioStreamIndex];
            AVStream* outStream = outputFmtCtx->streams[0];

            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, inStream->time_base, outStream->time_base);

            ret = av_interleaved_write_frame(outputFmtCtx, pkt);
            if (ret < 0) {
                brls::Logger::warning("concatenateAudioFiles: Error writing packet");
            }

            packetsWritten++;

            // Report progress periodically
            if (progressCallback && packetsWritten - lastProgressReport > 1000) {
                progressCallback(static_cast<int>(packetsWritten), 0);
                lastProgressReport = packetsWritten;
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // Write trailer
    av_write_trailer(outputFmtCtx);

    // Cleanup
    avformat_close_input(&inputFmtCtx);
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFmtCtx->pb);
    avformat_free_context(outputFmtCtx);

    // Remove concat list file
#ifdef __vita__
    sceIoRemove(concatListPath.c_str());
#else
    std::remove(concatListPath.c_str());
#endif

    brls::Logger::info("concatenateAudioFiles: Successfully combined {} files ({} packets)",
                       inputFiles.size(), packetsWritten);
    return true;
}

} // namespace vitaabs
