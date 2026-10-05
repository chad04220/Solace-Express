    if(partOn(40)) {
        // MANTIS angular graphite/amber research station. All details static.
        // floor cassette
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.385000,-3.350000),vec3(0.380000,0.025000,0.700000),0.008000),113.0));
        // seat floor rail
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.205000,-0.320000,-2.990000),vec3(0.022000,0.045000,0.280000),0.008000),119.0));
        // seat floor rail
        r=opU(r,vec2(sdRoundBox(p-vec3(0.205000,-0.320000,-2.990000),vec3(0.022000,0.045000,0.280000),0.008000),119.0));
        // seat pan carbon shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.205000,-2.980000),vec3(0.275000,0.045000,0.300000),0.008000),113.0));
        // seat pan cushion
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.140000,-2.980000),vec3(0.235000,0.055000,0.265000),0.025000),114.0));
        // seat back shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.175000,-2.615000),vec3(0.280000,0.320000,0.055000),0.008000),113.0));
        // seat back pad
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.175000,-2.680000),vec3(0.218000,0.300000,0.040000),0.018000),114.0));
        // lumbar side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.250000,0.040000,-2.755000),vec3(0.035000,0.175000,0.090000),0.016000),114.0));
        // pan side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.245000,-0.100000,-2.990000),vec3(0.022000,0.055000,0.230000),0.015000),114.0));
        // shoulder harness
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.100000,0.205000,-2.726000),vec3(0.026000,0.265000,0.010000),0.004000),116.0));
        // harness lower diagonal
        r=opU(r,vec2(sdCapsule(p,vec3(-0.100000,-0.055000,-2.726000),vec3(-0.055000,-0.110000,-2.830000),0.014000),116.0));
        // lap webbing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.115000,-0.075000,-2.860000),vec3(0.090000,0.010000,0.032000),0.004000),116.0));
        // lumbar side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(0.250000,0.040000,-2.755000),vec3(0.035000,0.175000,0.090000),0.016000),114.0));
        // pan side bolster
        r=opU(r,vec2(sdRoundBox(p-vec3(0.245000,-0.100000,-2.990000),vec3(0.022000,0.055000,0.230000),0.015000),114.0));
        // shoulder harness
        r=opU(r,vec2(sdRoundBox(p-vec3(0.100000,0.205000,-2.726000),vec3(0.026000,0.265000,0.010000),0.004000),116.0));
        // harness lower diagonal
        r=opU(r,vec2(sdCapsule(p,vec3(0.100000,-0.055000,-2.726000),vec3(0.055000,-0.110000,-2.830000),0.014000),116.0));
        // lap webbing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.115000,-0.075000,-2.860000),vec3(0.090000,0.010000,0.032000),0.004000),116.0));
        // harness release buckle
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.070000,-2.840000),vec3(0.036000,0.018000,0.040000),0.008000),119.0));
        // buckle release face
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.050000,-2.840000),vec3(0.025000,0.009000,0.027000),0.004000),111.0));
        // headrest shell
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.562000,-2.690000),vec3(0.178000,0.100000,0.065000),0.008000),113.0));
        // headrest cushion
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.562000,-2.762000),vec3(0.150000,0.083000,0.022000),0.014000),114.0));
        // headrest datum
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.075000,0.580000,-2.788000),vec3(0.013000,0.045000,0.009000),0.004000),115.0));
        // headrest datum
        r=opU(r,vec2(sdRoundBox(p-vec3(0.075000,0.580000,-2.788000),vec3(0.013000,0.045000,0.009000),0.004000),115.0));
        // pitch fixed pivot socket
        r=opU(r,vec2(sdRoundBox(p-vec3(0.320000,-0.315000,-3.150000),vec3(0.050000,0.060000,0.055000),0.008000),113.0));
        // throttle fixed pivot socket
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.370000,-0.302500,-3.120000),vec3(0.050000,0.072500,0.055000),0.008000),113.0));
        // pedal fixed slider rail
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.170000,-0.335000,-3.900000),vec3(0.030000,0.022000,0.240000),0.008000),119.0));
        // pedal fixed slider rail
        r=opU(r,vec2(sdRoundBox(p-vec3(0.170000,-0.335000,-3.900000),vec3(0.030000,0.022000,0.240000),0.008000),119.0));
        // side avionics console
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.488000,-0.160000,-3.490000),vec3(0.067000,0.070000,0.370000),0.008000),113.0));
        // side service touch slab
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.080000,-3.660000),vec3(0.049000,0.012000,0.130000),0.008000),117.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.400000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.340000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.489000,-0.075000,-3.280000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // console amber task strip
        r=opU(r,vec2(sdCapsule(p,vec3(-0.420000,-0.095000,-3.830000),vec3(-0.420000,-0.095000,-3.490000),0.010000),115.0));
        // rear vent housing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.120000,-2.660000),vec3(0.065000,0.160000,0.040000),0.008000),113.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,-0.005000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.045000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.095000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.145000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.195000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.384000,0.245000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // side structural longeron
        r=opU(r,vec2(sdCapsule(p,vec3(-0.552000,0.095000,-3.970000),vec3(-0.552000,0.095000,-2.940000),0.018000),119.0));
        // side avionics console
        r=opU(r,vec2(sdRoundBox(p-vec3(0.488000,-0.160000,-3.490000),vec3(0.067000,0.070000,0.370000),0.008000),113.0));
        // side service touch slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.080000,-3.660000),vec3(0.049000,0.012000,0.130000),0.008000),117.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.400000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.340000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // static test key
        r=opU(r,vec2(sdRoundBox(p-vec3(0.489000,-0.075000,-3.280000),vec3(0.029000,0.015000,0.019000),0.005000),119.0));
        // console amber task strip
        r=opU(r,vec2(sdCapsule(p,vec3(0.420000,-0.095000,-3.830000),vec3(0.420000,-0.095000,-3.490000),0.010000),115.0));
        // rear vent housing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.120000,-2.660000),vec3(0.065000,0.160000,0.040000),0.008000),113.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,-0.005000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.045000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.095000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.145000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.195000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // rear vent louver
        r=opU(r,vec2(sdRoundBox(p-vec3(0.384000,0.245000,-2.708000),vec3(0.050000,0.010000,0.010000),0.004000),118.0));
        // side structural longeron
        r=opU(r,vec2(sdCapsule(p,vec3(0.552000,0.095000,-3.970000),vec3(0.552000,0.095000,-2.940000),0.018000),119.0));
        // arch leg
        r=opU(r,vec2(sdCapsule(p,vec3(-0.555000,0.060000,-3.020000),vec3(-0.555000,0.420000,-3.020000),0.020000),113.0));
        // arch chamfer
        r=opU(r,vec2(sdCapsule(p,vec3(-0.555000,0.420000,-3.020000),vec3(-0.365000,0.675000,-3.020000),0.020000),113.0));
        // arch leg
        r=opU(r,vec2(sdCapsule(p,vec3(0.555000,0.060000,-3.020000),vec3(0.555000,0.420000,-3.020000),0.020000),113.0));
        // arch chamfer
        r=opU(r,vec2(sdCapsule(p,vec3(0.555000,0.420000,-3.020000),vec3(0.365000,0.675000,-3.020000),0.020000),113.0));
        // arch crown
        r=opU(r,vec2(sdCapsule(p,vec3(-0.365000,0.675000,-3.020000),vec3(0.365000,0.675000,-3.020000),0.020000),113.0));
        // ceiling task housing
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.215000,0.731000,-3.580000),vec3(0.070000,0.020000,0.200000),0.008000),113.0));
        // ceiling task diffuser
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.215000,0.706000,-3.580000),vec3(0.045000,0.010000,0.170000),0.005000),115.0));
        // ceiling task housing
        r=opU(r,vec2(sdRoundBox(p-vec3(0.215000,0.731000,-3.580000),vec3(0.070000,0.020000,0.200000),0.008000),113.0));
        // ceiling task diffuser
        r=opU(r,vec2(sdRoundBox(p-vec3(0.215000,0.706000,-3.580000),vec3(0.045000,0.010000,0.170000),0.005000),115.0));
        // instrument blade
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.050000,-4.055000),vec3(0.435000,0.175000,0.045000),0.008000),113.0));
        // instrument surround
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.266000,0.070000,-4.000000),vec3(0.140000,0.115000,0.025000),0.008000),119.0));
        // instrument face
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.266000,0.070000,-3.970000),vec3(0.118000,0.093000,0.012000),0.008000),117.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.180000,-0.100000,-3.997000),vec3(-0.180000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.250000,-0.100000,-3.997000),vec3(-0.250000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(-0.320000,-0.100000,-3.997000),vec3(-0.320000,-0.100000,-3.967000),0.011000),119.0));
        // switch guard
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.270000,-0.145000,-3.980000),vec3(0.120000,0.010000,0.025000),0.004000),111.0));
        // instrument surround
        r=opU(r,vec2(sdRoundBox(p-vec3(0.266000,0.070000,-4.000000),vec3(0.140000,0.115000,0.025000),0.008000),119.0));
        // instrument face
        r=opU(r,vec2(sdRoundBox(p-vec3(0.266000,0.070000,-3.970000),vec3(0.118000,0.093000,0.012000),0.008000),117.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.180000,-0.100000,-3.997000),vec3(0.180000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.250000,-0.100000,-3.997000),vec3(0.250000,-0.100000,-3.967000),0.011000),119.0));
        // static switch stem
        r=opU(r,vec2(sdCapsule(p,vec3(0.320000,-0.100000,-3.997000),vec3(0.320000,-0.100000,-3.967000),0.011000),119.0));
        // switch guard
        r=opU(r,vec2(sdRoundBox(p-vec3(0.270000,-0.145000,-3.980000),vec3(0.120000,0.010000,0.025000),0.004000),111.0));
        // center systems panel
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.050000,-3.990000),vec3(0.082000,0.112000,0.027000),0.008000),113.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,-0.025000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.025000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.075000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // center annunciator
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.125000,-3.953000),vec3(0.051000,0.012000,0.011000),0.004000),115.0));
        // front camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.410000,-4.135000),vec3(0.410000,0.177000,0.012000),0.006000),112.0));
        // front pane side frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.428000,0.410000,-4.128000),vec3(0.012000,0.190000,0.023000),0.004000),119.0));
        // front pane side frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.428000,0.410000,-4.128000),vec3(0.012000,0.190000,0.023000),0.004000),119.0));
        // front pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.215000,-4.128000),vec3(0.436000,0.010000,0.023000),0.004000),119.0));
        // front pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.000000,0.605000,-4.128000),vec3(0.436000,0.010000,0.023000),0.004000),119.0));
        // side camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.559000,0.365000,-3.510000),vec3(0.012000,0.172000,0.370000),0.006000),112.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.365000,-3.896000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.365000,-3.124000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.176000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(-0.551000,0.554000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side camera slab
        r=opU(r,vec2(sdRoundBox(p-vec3(0.559000,0.365000,-3.510000),vec3(0.012000,0.172000,0.370000),0.006000),112.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.365000,-3.896000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane end frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.365000,-3.124000),vec3(0.023000,0.185000,0.010000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.176000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // side pane horizontal frame
        r=opU(r,vec2(sdRoundBox(p-vec3(0.551000,0.554000,-3.510000),vec3(0.023000,0.011000,0.390000),0.004000),119.0));
        // ceiling fixture carrier
        r=opU(r,vec2(sdCapsule(p,vec3(-0.215000,0.715000,-3.580000),vec3(-0.215000,0.675000,-3.020000),0.014000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(-0.470000,-0.200000,-3.750000),vec3(-0.340000,-0.365000,-3.750000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.550000,-0.160000,-3.750000),vec3(-0.552000,0.095000,-3.750000),0.016000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(-0.470000,-0.200000,-3.350000),vec3(-0.340000,-0.365000,-3.350000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.550000,-0.160000,-3.350000),vec3(-0.552000,0.095000,-3.350000),0.016000),113.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.551000,0.170000,-3.800000),vec3(-0.552000,0.095000,-3.800000),0.015000),119.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.551000,0.170000,-3.200000),vec3(-0.552000,0.095000,-3.200000),0.015000),119.0));
        // instrument floor support
        r=opU(r,vec2(sdCapsule(p,vec3(-0.300000,-0.100000,-4.020000),vec3(-0.300000,-0.365000,-3.980000),0.018000),113.0));
        // vent seat bracket
        r=opU(r,vec2(sdCapsule(p,vec3(-0.250000,0.120000,-2.650000),vec3(-0.380000,0.120000,-2.650000),0.016000),113.0));
        // ceiling fixture carrier
        r=opU(r,vec2(sdCapsule(p,vec3(0.215000,0.715000,-3.580000),vec3(0.215000,0.675000,-3.020000),0.014000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(0.470000,-0.200000,-3.750000),vec3(0.340000,-0.365000,-3.750000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(0.550000,-0.160000,-3.750000),vec3(0.552000,0.095000,-3.750000),0.016000),113.0));
        // console floor outrigger
        r=opU(r,vec2(sdCapsule(p,vec3(0.470000,-0.200000,-3.350000),vec3(0.340000,-0.365000,-3.350000),0.018000),113.0));
        // sill console support
        r=opU(r,vec2(sdCapsule(p,vec3(0.550000,-0.160000,-3.350000),vec3(0.552000,0.095000,-3.350000),0.016000),113.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.551000,0.170000,-3.800000),vec3(0.552000,0.095000,-3.800000),0.015000),119.0));
        // pane sill bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.551000,0.170000,-3.200000),vec3(0.552000,0.095000,-3.200000),0.015000),119.0));
        // instrument floor support
        r=opU(r,vec2(sdCapsule(p,vec3(0.300000,-0.100000,-4.020000),vec3(0.300000,-0.365000,-3.980000),0.018000),113.0));
        // vent seat bracket
        r=opU(r,vec2(sdCapsule(p,vec3(0.250000,0.120000,-2.650000),vec3(0.380000,0.120000,-2.650000),0.016000),113.0));
    }
