# Earth Orbital 100 Million Year Precession and Tilt Solutions
#----------------------------------------------------------------------- 
#                World Data Service for Paleoclimatology, Boulder 
#                                  and 
#                     NOAA Paleoclimatology Program 
#             National Centers for Environmental Information (NCEI)
#----------------------------------------------------------------------- 
# Template Version 4.0
# Encoding: UTF-8
# NOTE: Please cite original publication, NOAA Landing Page URL, dataset and publication DOIs (where available), and date accessed when using downloaded data. If there is no publication information, please cite investigator, study title, NOAA Landing Page URL, and date accessed. 
#
# Description/Documentation lines begin with #
# Data lines have no #
#
# NOAA_Landing_Page: https://www.ncdc.noaa.gov/paleo/study/35174 
#   Landing_Page_Description: 
#
# Study_Level_JSON_Metadata: https://www.ncei.noaa.gov/pub/data/metadata/published/paleo/json/noaa-forcing-35174.json 
#   Study_Level_JSON_Description: JSON metadata of this data file's parent study, which includes all study metadata.
#
# Data_Type: Climate Forcing
#
# Dataset_DOI: 
#
# Science_Keywords: Milankovitch 
#--------------------     
# Resource_Links
#   
# Data_Download_Resource: https://www.ncei.noaa.gov/pub/data/paleo/climate_forcing/orbital_variations/zeebe2022/readme-zeebe2022.txt
#   Data_Download_Description: Data Description; this file
#
# Supplemental_Download_Resource: https://www.ncei.noaa.gov/pub/data/paleo/climate_forcing/orbital_variations/zeebe2022/
#   Supplemental_Description: Data file folder; Orbital solutions 
#
# Related_Online_Resource: 
#   Related_Online_Description: 
#
# Original_Source_URL: https://www.soest.hawaii.edu/oceanography/faculty/zeebe_files/Astro/PrecTilt/
#   Original_Source_Description: Source URL for binary data and code
#--------------------
# Contribution_Date
#    Date: 2022-01-03
#--------------------
# File_Last_Modified_Date
#     Date: 2022-01-03
#--------------------
# Title
#   Study_Name: Earth Orbital 100 Million Year Precession and Tilt Solutions
#--------------------
# Investigators
#   Investigators: Zeebe, R.E.; Lourens, L.J.
#--------------------
# Description_Notes_and_Keywords
#   Description: Pre-computed Precession-Tilt solutions for the past 100 million years from the ZB18a model (Zeebe and Lourens, Science, 2019). 
#	Provided Keywords: Astronomical Forcing
#	Code used to generate these solutions, or others with user-defined parameters, is available from: 
#	https://www.soest.hawaii.edu/oceanography/faculty/zeebe_files/Astro/PrecTilt/ 
#	Data file naming pattern is: PT.Dex.xxxxTdy.yyyy.ext is the PT solution for ZB18a using dynamical ellipticity Ed = x.xxxx and tidal dissipation Td = y.yyyy. 
#	File contents are: 
#	time                   [kyr] in the past
#	obliquity              [radian]
#	precession             [radian] from ECLIPJ2000
#	climatic precession    [] e*sin(\varpi*)
#	where \varpi* is the longitude of perihelion relative to the moving equinox.
#	CAUTION: The output time intervals are not equally spaced. If required for e.g.,spectral analysis, the user needs to interpolate to equally spaced time intervals.
#--------------------
# Publication 
#   Authors: Richard E. Zeebe and Lucas J. Lourens
#   Published_Date_or_Year: 2022 in press
#   Published_Title: A deep-time dating tool for paleo-applications utilizing obliquity and precession cycles: The role of dynamical ellipticity and tidal dissipation
#   Journal_Name: Paleoceanography and Paleoclimatology
#   Volume: 
#   Edition: 
#   Issue: 
#   Pages:
#   Report_Number: 
#   DOI: 
#   Publication_Place: 
#   Publisher:
#   ISBN: 
#   Online_Resource: 
#   Other_Reference_Details: 
#   Full_Citation:
#   Abstract: Pre-Pleistocene age models used in paleoceanography and paleoclimatology often rely on the imprint of astronomically calculated cycles of eccentricity and other solar system frequencies in sedimentary records (405, 173, and 100kyr). However, use of obliquity and precession cycles (at present ~41 and ~20 kyr) remains challenging for these periods, mostly due to past changes in Earth's dynamical ellipticity (Ed, gravitational shape) and tidal dissipation (Td, slowdown of Earth's rotation), which affect the astronomical calculations. Here we present a dating method for deep-time records by integrating Ed and Td into astrochronology. The key to our approach is the combination of constraints on Td (and thus indirectly on Ed) with age model optimization based on solar system frequencies, plus tuning to obliquity/precession frequencies, while varying Td and Ed. Importantly, we target deep-time intervals where Td\ shows significant effects but high-quality sedimentary records are available (early Cenozoic). We include a quickstart guide to our approach and make our code and pre-computed solutions freely available for users. To demonstrate the practical utility of our approach, we apply our tool to two case studies using deep-sea records from the early and middle Eocene. Our results confirm very accurate chronologies of sedimentary records from the early Eocene (~56-54Ma) but suggest significant improvement for the middle Eocene (~40-39Ma). For the early Eocene, our method provides absolute geologic ages with an estimated uncertainty of +-20 to 40kyr, which is smaller than or equal to typical uncertainties from recent radiometric Ar/Ar dating.
#--------------------
# Funding_Agency 
#   Funding_Agency_Name: US National Science Foundation
#   Grant: OCE20-01022, OCE20-34660
#--------------------
# Funding_Agency 
#   Funding_Agency_Name: Heising-Simons Foundation
#   Grant: 2021-2800
#--------------------
# Site_Information 
#   Site_Name: Global
#   Location: Geographic Region>Global
#   Northernmost_Latitude: 90
#   Southernmost_Latitude: -90
#   Easternmost_Longitude: 180
#   Westernmost_Longitude: -180
#   Elevation_m: 
#--------------------
# Data_Collection  
#   Collection_Name: Zeebe2022orbital
#   Earliest_Year: 100000000
#   Most_Recent_Year: 0
#   Time_Unit: Cal. year BP
#   Core_Length_m: 
#   Parameter_Keywords: other
#   Notes:
#--------------------
# Chronology_Information
#   Chronology: 
#
#--------------------
# Variables  
#
# PaST_Thesaurus_Download_Resource:  https://www.ncei.noaa.gov/pub/data/paleo/PaST-thesaurus/SKOS/past-thesaurus-v1.0.rdf
#   PaST_Thesaurus_Download_Description: Paleoenvironmental Standard Terms (PaST) Thesaurus terms, definitions, and relationships in SKOS format.
#
# Variables format: Short_name <tab> what,material,error,units,seasonality,data_type,detail,method,data_format,additional_information
#
## time_kyr	age, , , calendar kiloyear before present, , climate forcing, , ,N,
## obliquity_rad	orbital parameter, , , radian, , climate forcing, , ,N, obliquity
## precession_rad	orbital parameter, , , radian, , climate forcing, , ,N, precession from ECLIPJ2000
## climatic_precession 	orbital parameter, , , radian, , climate forcing, , ,N, climatic precession; e*sin(\varpi*)\varpi* = longitude of perihelion from moving equinox. 
#
#--------------------
# Data:
# Data lines follow (have no #)
# Data line format - tab-delimited text, variable short name as header
# Missing_Values: 
#
time_kyr	obliquity_rad	precession_rad	climatic_precession