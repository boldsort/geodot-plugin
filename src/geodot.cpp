#include "geodot.h"
#include "RasterTileExtractor.h"

#include <algorithm>  // For std::clamp
#include <functional> // For std::hash

using namespace godot;

void Geodot::_init() {
    RasterTileExtractor::initialize();
    VectorExtractor::initialize();
}

void Geodot::_register_methods() {
    register_method("get_dataset", &Geodot::get_dataset);
    register_method("get_raster_layer", &Geodot::get_raster_layer);
    register_method("get_raster_layer_for_pyramid", &Geodot::get_raster_layer_for_pyramid);
}

Ref<GeoDataset> Geodot::get_dataset(String path) {
    Ref<GeoDataset> dataset;
    dataset.instance();

    dataset->load_from_file(path);

    return dataset;
}

Ref<GeoRasterLayer> Geodot::get_raster_layer(String path) {
    Ref<GeoRasterLayer> layer;
    layer.instance();

    layer->load_from_file(path);

    return layer;
}

Ref<GeoRasterLayer> Geodot::get_raster_layer_for_pyramid(String root_folder, String image_ending) {
    Ref<PyramidGeoRasterLayer> layer = Ref<PyramidGeoRasterLayer>(PyramidGeoRasterLayer::_new());

    layer->set_pyramid_base(root_folder);
    layer->set_file_ending(image_ending);

    return layer;
}
