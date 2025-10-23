@echo off
echo Copying compute shaders to output directory...

if not exist "src\project\msvc2017\output\baseq3\shaders\compute" (
    mkdir "src\project\msvc2017\output\baseq3\shaders\compute"
)

copy "baseq3\shaders\compute\linearize_depth.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\reconstruct_normals.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\rt_composite.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\rtx_debug_overlay.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\taa_resolve.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\taa_sharpen.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y
copy "baseq3\shaders\compute\gpu_culling.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y

echo Done copying compute shaders.