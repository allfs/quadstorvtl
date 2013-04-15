<!-- 

function changeauto()
{
	if (document.addvol.autoconf.checked == true)
		document.addvol.barcode.disabled = true;
	else
		document.addvol.barcode.disabled = false;
}

function isNumeric(str){
	var re = /[\D]/
	if (re.test(str))
	{
		return false;
	}
	return true;
}

function ValidString(str){
	var re = /[^a-zA-Z0-9]/

	if (re.test(str))
	{
		return false;
	}
	return true;
}

function checkform()
{

	if (document.addvol.vtltype.value == 1)
	{
		if ((!document.addvol.autoconf || document.addvol.autoconf.checked != true) && (!document.addvol.barcode.value))
		{
			alert("Media label cannot be empty");
			return false;
		}
	}
	else
	{
		if (!document.addvol.barcode.value)
		{
			alert("Media label cannot be empty");
			return false;
		}
	}

	if (!ValidString(document.addvol.barcode.value))
	{
		alert("Media label can only contains alphabets or numbers");
		return false;
	}

	if (!document.addvol.vtlname.value)
	{
		alert("VTL Name cannot be empty");
		return false;
	}

	if (!ValidString(document.addvol.vtlname.value))
	{
		alert("VTL Name can only contains alphabets or numbers");
	}

	if (!document.addvol.nvolumes.value)
	{
		alert("Number of volumes cannot be empty");
		return false;
	}

	if (!isNumeric(document.addvol.nvolumes.value))
	{
		alert("Number of volumes should be a number");
		return false;
	}

	var nvolumes = parseInt(document.addvol.nvolumes.value);

	if (nvolumes <= 0)
	{
		alert("Number of volumes has to be a number greater than zero");
		return false;
	}

	if (nvolumes > 512)
	{
		alert("Number of media has to be a number less than 512");
		return false;
	}

	if (document.addvol.autoconf.checked != true  && document.addvol.barcode.value.length != 6 && nvolumes != 1)
	{
		alert("Media label start range has to be 6 characters");
		return false;
	}
	return true;
}

// -->
