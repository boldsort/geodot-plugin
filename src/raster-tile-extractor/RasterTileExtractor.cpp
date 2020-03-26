#include <iostream>
#include "RasterTileExtractor.h"
#include <gdal/gdal_priv.h>
#include <gdal/gdalwarper.h>
#include <filesystem>

void RasterTileExtractor::initialize() {
    // Register all drivers - without this, GDALGetDriverByName doesn't work
    GDALAllRegister();
}

void RasterTileExtractor::reproject_to_webmercator(const char *base_path, const char *outfile) {
    GDALDriverH hDriver;
    GDALDataType eDT;
    GDALDatasetH hDstDS;
    GDALDatasetH hSrcDS;

    // Open the source file.
    hSrcDS = GDALOpen(base_path, GA_ReadOnly);
    CPLAssert(hSrcDS != NULL)

    // Create output with same datatype as first input band.
    eDT = GDALGetRasterDataType(GDALGetRasterBand(hSrcDS, 1));


    hDriver = GDALGetDriverByName("GTiff");
    CPLAssert(hDriver != NULL)

    // Get Source coordinate system.
    const char *pszSrcWKT, *pszDstWKT = nullptr;
    pszSrcWKT = GDALGetProjectionRef(hSrcDS);
    CPLAssert(pszSrcWKT != NULL && strlen(pszSrcWKT) > 0)

    // Get Webmercator coordinate system
    OGRSpatialReference oSRS;
    oSRS.importFromEPSG(3857);
    oSRS.exportToWkt(const_cast<char **>(&pszDstWKT));

    // Create a transformer that maps from source pixel/line coordinates
    // to destination georeferenced coordinates (not destination
    // pixel line).  We do that by omitting the destination dataset
    // handle (setting it to NULL).
    void *hTransformArg;
    hTransformArg =
            GDALCreateGenImgProjTransformer(hSrcDS, pszSrcWKT, nullptr, pszDstWKT,
                                            FALSE, 0, 1);
    CPLAssert(hTransformArg != NULL)

    // Get approximate output georeferenced bounds and resolution for file.
    double adfDstGeoTransform[6];
    int nPixels = 0, nLines = 0;
    CPLErr eErr;
    eErr = GDALSuggestedWarpOutput(hSrcDS,
                                   GDALGenImgProjTransform, hTransformArg,
                                   adfDstGeoTransform, &nPixels, &nLines);
    CPLAssert(eErr == CE_None)

    GDALDestroyGenImgProjTransformer(hTransformArg);

    // Create the output file.
    hDstDS = GDALCreate(hDriver, outfile, nPixels, nLines,
                        GDALGetRasterCount(hSrcDS), eDT, nullptr);
    CPLAssert(hDstDS != NULL)

    // Write out the projection definition.
    GDALSetProjection(hDstDS, pszDstWKT);
    GDALSetGeoTransform(hDstDS, adfDstGeoTransform);

    // Copy the color table, if required.
    // TODO: Only supports one raster band - are more required?
    GDALColorTableH hCT;
    hCT = GDALGetRasterColorTable(GDALGetRasterBand(hSrcDS, 1));
    if (hCT != nullptr)
        GDALSetRasterColorTable(GDALGetRasterBand(hDstDS, 1), hCT);

    int band_count = GDALGetRasterCount(hSrcDS);

    // Setup warp options.
    GDALWarpOptions *psWarpOptions = GDALCreateWarpOptions();
    psWarpOptions->hSrcDS = hSrcDS;
    psWarpOptions->hDstDS = hDstDS;
    psWarpOptions->nBandCount = band_count;
    psWarpOptions->panSrcBands =
            (int *) CPLMalloc(sizeof(int) * psWarpOptions->nBandCount);
    psWarpOptions->panSrcBands[0] = band_count;
    psWarpOptions->panDstBands =
            (int *) CPLMalloc(sizeof(int) * psWarpOptions->nBandCount);
    psWarpOptions->panDstBands[0] = band_count;
    psWarpOptions->pfnProgress = GDALTermProgress;

    // Establish reprojection transformer.
    psWarpOptions->pTransformerArg =
            GDALCreateGenImgProjTransformer(hSrcDS,
                                            GDALGetProjectionRef(hSrcDS),
                                            hDstDS,
                                            GDALGetProjectionRef(hDstDS),
                                            FALSE, 0.0, 1);
    psWarpOptions->pfnTransformer = GDALGenImgProjTransform;

    // Initialize and execute the warp operation.
    GDALWarpOperation oOperation;
    oOperation.Initialize(psWarpOptions);
    oOperation.ChunkAndWarpImage(0, 0,
                                 GDALGetRasterXSize(hDstDS),
                                 GDALGetRasterYSize(hDstDS));
    GDALDestroyGenImgProjTransformer(psWarpOptions->pTransformerArg);
    GDALDestroyWarpOptions(psWarpOptions);

    GDALClose(hDstDS);
    GDALClose(hSrcDS);
}

GeoRaster *RasterTileExtractor::clip(const char *base_path, double top_left_x, double top_left_y, double size_meters, int img_size,
                          int interpolation_type) {
    GDALDataset *source, *dest;
    GDALDriver *driver;

    // TODO: Add the option of saving to a GTiff for caching
    if (false) {
        driver = (GDALDriver *)GDALGetDriverByName("GTiff");
    } else {
        driver = (GDALDriver *)GDALGetDriverByName("MEM");
    }

    source = (GDALDataset *) GDALOpen(base_path, GA_ReadOnly);
    GDALDataType datatype = source->GetRasterBand(1)->GetRasterDataType();

    int band_count = source->GetRasterCount();

    // Get the current Transform of the source image
    double transform[6];
    source->GetGeoTransform(transform);

    // Adjust the top left coordinates according to the input variables
    double previous_top_left_x = transform[0];
    double previous_top_left_y = transform[3];
    double pixel_size = transform[1];

    // We need to transform the previous top left coordinates to webmercator
    OGRSpatialReference *source_sr = new OGRSpatialReference();
    source_sr->importFromWkt(source->GetProjectionRef());

    OGRSpatialReference *destination_sr = new OGRSpatialReference();
    destination_sr->importFromEPSG(3857);

    OGRCoordinateTransformation *coordinateTransformation = OGRCreateCoordinateTransformation(source_sr, destination_sr);

    coordinateTransformation->Transform(1, &previous_top_left_x, &previous_top_left_y);

    // Get the offset in meters from the previous to left to the new top left
    // TODO: Only works for Austria - we need to check where to subtract from or something like that
    double offset_meters_x = top_left_x - previous_top_left_x;
    double offset_meters_y = previous_top_left_y - top_left_y;

    // FIXME: This is because we need to convert between Webmercator meters and true meters or something like that...
    //  obviously this is a hack and needs to be done properly!
    offset_meters_x *= 0.66;
    offset_meters_y *= 0.66;

    // Convert meters to pixels using the pixel size in meters
    int offset_pixels_x = static_cast<int>(offset_meters_x / pixel_size);
    int offset_pixels_y = static_cast<int>(offset_meters_y / pixel_size);

    // Calculate the desired size in pixels
    int size_pixels  = static_cast<int>(size_meters / pixel_size);

    // With these parameters, we can construct a GeoRaster!
    return new GeoRaster(source, offset_pixels_x, offset_pixels_y, size_pixels, img_size);
}

#define PYRAMID_DIRECTORY_ENDING "pyramid"

GeoRaster *
RasterTileExtractor::get_raster_at_position(const char *base_path, const char *file_ending, double top_left_x,
                                            double top_left_y,
                                            double size_meters, int img_size, int interpolation_type) {
    // First, check if we have a pre-tiled pyramid of this data
    std::string pyramid_name_string = std::string(base_path) + "." + std::string(PYRAMID_DIRECTORY_ENDING);

    if (std::filesystem::exists(pyramid_name_string)) {
        // We have a pre-tiled pyramid
        GDALDataset *dataset_from_pyramid = get_from_pyramid(pyramid_name_string.c_str(), file_ending, top_left_x,
                                                             top_left_y, size_meters, img_size, interpolation_type);

        if (dataset_from_pyramid != nullptr) {
            return new GeoRaster(dataset_from_pyramid);
        }
    } else {
        // Check if there is a single image with the given path
        std::string raster_path_string = std::string(base_path) + "." + std::string(file_ending);

        if (std::filesystem::exists(raster_path_string)) {
            return clip(raster_path_string.c_str(), top_left_x, top_left_y, size_meters, img_size,
                                      interpolation_type);
        }
    }

    // If there was neither a single file nor a pyramid, return null
    return nullptr;
}

#define WEBMERCATOR_MAX 20037508.0
#define PI 3.14159265358979323846
#define CIRCUMEFERENCE 40075016.686

class path;

GDALDataset *
RasterTileExtractor::get_from_pyramid(const char *base_path, const char *file_ending, double top_left_x,
                                      double top_left_y, double size_meters,
                                      int img_size, int interpolation_type) {
    // Norm webmercator position (in meters) to value between -1 and 1
    double norm_x = 0.5 + ((top_left_x + size_meters / 2.0) / WEBMERCATOR_MAX) * 0.5;

    double norm_y = 1.0 - (0.5 + ((top_left_y - size_meters / 2.0) / WEBMERCATOR_MAX) * 0.5);

    // Get latitude and use it to calculate the zoom level here
    double latitude = 0.81777; // TODO: Finding the actual latitude requires a more complex calculation due to projection
    // Original formula: size = C * cos(latitude) / pow(2, zoom_level) (from https://wiki.openstreetmap.org/wiki/Zoom_levels)
    int zoom_level = (int) round(log2(CIRCUMEFERENCE * cos(latitude) / size_meters)) + 1;

    // Number of tiles at this zoom level
    int num_tiles = pow(2.0, zoom_level);

    // Tile coordinates in OSM pyramid
    int tile_x = (int) floor(norm_x * num_tiles);
    int tile_y = (int) floor(norm_y * num_tiles);

    // Build the complete path
    std::filesystem::path path = std::filesystem::path(base_path);
    path /= std::filesystem::path(std::to_string(zoom_level));
    path /= std::filesystem::path(std::to_string(tile_x));
    path /= std::filesystem::path(std::to_string(tile_y));
    path += ".";
    path += file_ending;

    // Load the result as a GDALDataset and return it
    return (GDALDataset *) GDALOpen(path.c_str(), GA_ReadOnly);
}
